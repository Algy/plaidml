// Copyright 2018, Intel Corporation

#pragma once

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <boost/optional.hpp>

#include "tile/base/shape.h"
#include "tile/math/polynomial.h"
#include "tile/stripe/stripe.pb.h"

namespace vertexai {
namespace tile {
namespace stripe {

using Affine = math::Polynomial<int64_t>;

enum class StmtKind {
  Load,
  Store,
  Constant,
  Special,
  Intrinsic,
  Block,
};

struct Load;
struct Store;
struct Constant;
struct Special;
struct Intrinsic;
struct Block;

struct ConstStmtVisitor {
  virtual void Visit(const Load&) = 0;
  virtual void Visit(const Store&) = 0;
  virtual void Visit(const Constant&) = 0;
  virtual void Visit(const Special&) = 0;
  virtual void Visit(const Intrinsic&) = 0;
  virtual void Visit(const Block&) = 0;
};

struct MutableStmtVisitor {
  virtual void Visit(Load*) = 0;
  virtual void Visit(Store*) = 0;
  virtual void Visit(Constant*) = 0;
  virtual void Visit(Special*) = 0;
  virtual void Visit(Intrinsic*) = 0;
  virtual void Visit(Block*) = 0;
};

struct RewriteStmtVisitor {
  virtual Load* Visit(const Load&) = 0;
  virtual Store* Visit(const Store&) = 0;
  virtual Constant* Visit(const Constant&) = 0;
  virtual Special* Visit(const Special&) = 0;
  virtual Intrinsic* Visit(const Intrinsic&) = 0;
  virtual Block* Visit(const Block&) = 0;
};

struct Statement;

using StatementList = std::list<std::shared_ptr<Statement>>;
using StatementIt = StatementList::iterator;
using Tags = std::set<std::string>;

struct Taggable {
  // Generic properties used by optimization passes
  Tags tags;

  void set_tag(const std::string& tag) { tags.emplace(tag); }
  void add_tags(const Tags& to_add) { tags.insert(to_add.begin(), to_add.end()); }

  bool has_tag(const std::string& tag) const { return tags.count(tag) != 0; }
  bool has_tags(const Tags& to_find) const {
    for (const auto& tag : to_find) {
      if (tags.count(tag) == 0) {
        return false;
      }
    }
    return true;
  }
};

struct Statement : Taggable {
  virtual ~Statement() = default;
  virtual StmtKind kind() const = 0;
  virtual std::vector<std::string> buffer_reads() const { return {}; }
  virtual std::vector<std::string> buffer_writes() const { return {}; }
  virtual std::vector<std::string> scalar_uses() const { return {}; }
  virtual std::vector<std::string> scalar_defs() const { return {}; }
  virtual void Accept(ConstStmtVisitor*) const = 0;
  virtual void Accept(MutableStmtVisitor*) = 0;
  virtual Statement* Accept(RewriteStmtVisitor*) = 0;

  // The set of statements within the same Block that must complete
  // before this statement is evaluated.
  std::list<StatementIt> deps;
};

struct Index : Taggable {
  Index(const std::string& name,  //
        uint64_t range,           //
        const Affine& affine = Affine{})
      : name(name),  //
        range(range),
        affine(affine) {}
  std::string name;
  uint64_t range;
  Affine affine;
};

enum class RefDir {
  None = 0,
  In = 1,
  Out = 2,
  InOut = 3,
};

inline bool IsReadDir(const RefDir& dir) {  //
  return dir == RefDir::In || dir == RefDir::InOut;
}

inline bool IsWriteDir(const RefDir& dir) {  //
  return dir == RefDir::Out || dir == RefDir::InOut;
}

inline RefDir UnionDir(const RefDir& a, const RefDir& b) {  //
  return RefDir(static_cast<int>(a) | static_cast<int>(b));
}

struct Location {
  std::string name;
  Affine unit;
};

struct BankDimension {
  size_t dim_pos;
};

struct Refinement : Taggable {
  Refinement() {}
  Refinement(RefDir dir,                             //
             const std::string& from,                //
             const std::string& into,                //
             const std::vector<Affine>& access,      //
             const TensorShape& shape,               //
             const std::string& agg_op = "",         //
             const Location& location = Location{},  //
             bool is_const = false,                  //
             uint64_t offset = 0,                    //
             const boost::optional<BankDimension>& bank_dim = boost::none,
             const boost::optional<Affine>& cache_unit = boost::none)
      : dir(dir),
        from(from),
        into(into),
        access(access),
        interior_shape(shape),
        agg_op(agg_op),
        location(location),
        is_const(is_const),
        offset(offset),
        bank_dim(bank_dim),
        cache_unit(cache_unit) {}

  RefDir dir = RefDir::None;
  std::string from;
  std::string into;
  std::vector<Affine> access;
  TensorShape interior_shape;
  std::string agg_op;
  Location location;
  bool is_const = false;
  uint64_t offset = 0;                      // Offset within the location's arena.
  boost::optional<BankDimension> bank_dim;  // Which dimension should we bank on
  boost::optional<Affine> cache_unit;       // Which cache we should use when encaching this refinement

  Affine FlatAccess() const;
  TensorShape ApplyTile(const std::map<std::string, size_t>& tile_by_name) const;
};

struct Load : Statement {
  Load(const std::string& from, const std::string& into) : from(from), into(into) {}
  static std::shared_ptr<Load> Downcast(const std::shared_ptr<Statement>& stmt);
  StmtKind kind() const { return StmtKind::Load; }
  std::vector<std::string> buffer_reads() const { return {from}; }
  std::vector<std::string> scalar_defs() const { return {into}; }
  void Accept(ConstStmtVisitor* v) const { v->Visit(*this); }
  void Accept(MutableStmtVisitor* v) { v->Visit(this); }
  Load* Accept(RewriteStmtVisitor* v) { return v->Visit(*this); }

  std::string from;
  std::string into;
};

struct Store : Statement {
  Store(const std::string& from, const std::string& into) : from(from), into(into) {}
  static std::shared_ptr<Store> Downcast(const std::shared_ptr<Statement>& stmt);
  StmtKind kind() const { return StmtKind::Store; }
  std::vector<std::string> buffer_writes() const { return {into}; }
  std::vector<std::string> scalar_uses() const { return {from}; }
  void Accept(ConstStmtVisitor* v) const { v->Visit(*this); }
  void Accept(MutableStmtVisitor* v) { v->Visit(this); }
  Store* Accept(RewriteStmtVisitor* v) { return v->Visit(*this); }

  std::string from;
  std::string into;
};

struct Intrinsic : Statement {
  static std::shared_ptr<Intrinsic> Downcast(const std::shared_ptr<Statement>& stmt);
  StmtKind kind() const { return StmtKind::Intrinsic; }
  std::vector<std::string> scalar_uses() const { return inputs; }
  std::vector<std::string> scalar_defs() const { return outputs; }
  void Accept(ConstStmtVisitor* v) const { v->Visit(*this); }
  void Accept(MutableStmtVisitor* v) { v->Visit(this); }
  Intrinsic* Accept(RewriteStmtVisitor* v) { return v->Visit(*this); }

  std::string name;
  DataType type = DataType::FLOAT32;
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;

  static const char* ASSIGN;
  static const char* SUM;
  static const char* MIN;
  static const char* MAX;
  static const char* PROD;

  static const char* MUL;
  static const char* ADD;
  static const char* EQ;
  static const char* COND;
};

struct Special : Statement {
  static std::shared_ptr<Special> Downcast(const std::shared_ptr<Statement>& stmt);
  StmtKind kind() const { return StmtKind::Special; }
  std::vector<std::string> buffer_reads() const { return inputs; }
  std::vector<std::string> buffer_writes() const { return outputs; }
  void Accept(ConstStmtVisitor* v) const { v->Visit(*this); }
  void Accept(MutableStmtVisitor* v) { v->Visit(this); }
  Special* Accept(RewriteStmtVisitor* v) { return v->Visit(*this); }

  std::string name;
  std::vector<std::string> params;
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;

  static const char* ZERO;
  static const char* COPY;
};

enum class ConstType {
  Integer,
  Float,
};

struct Constant : Statement {
  Constant(const std::string& name, int64_t value) : name(name), type(ConstType::Integer), iconst(value) {}
  Constant(const std::string& name, double value) : name(name), type(ConstType::Float), fconst(value) {}
  static std::shared_ptr<Constant> Downcast(const std::shared_ptr<Statement>& stmt);
  StmtKind kind() const { return StmtKind::Constant; }
  std::vector<std::string> scalar_defs() const { return {name}; }
  void Accept(ConstStmtVisitor* v) const { v->Visit(*this); }
  void Accept(MutableStmtVisitor* v) { v->Visit(this); }
  Constant* Accept(RewriteStmtVisitor* v) { return v->Visit(*this); }

  std::string name;
  ConstType type;
  int64_t iconst;
  double fconst;
};

struct Block : Statement {
  static std::shared_ptr<Block> Downcast(const std::shared_ptr<Statement>& stmt);
  StmtKind kind() const { return StmtKind::Block; }
  std::vector<std::string> buffer_reads() const;
  std::vector<std::string> buffer_writes() const;
  void Accept(ConstStmtVisitor* v) const { v->Visit(*this); }
  void Accept(MutableStmtVisitor* v) { v->Visit(this); }
  Block* Accept(RewriteStmtVisitor* v) { return v->Visit(*this); }

  std::string name;
  std::string comments;
  std::vector<Index> idxs;
  std::vector<Affine> constraints;
  std::vector<Refinement> refs;
  StatementList stmts;
  Location location;

  // Helper methods
  std::vector<const Refinement*> ref_ins() const;
  std::vector<const Refinement*> ref_outs() const;
  Index* idx_by_name(const std::string& name);
  const Index* idx_by_name(const std::string& name) const;
  std::set<const Index*> accumulation_idxs() const;
  size_t idxs_product() const;
  // Find which refinement has an into called 'name'
  std::vector<Refinement>::iterator ref_by_into(const std::string& name, bool fail = true);
  std::vector<Refinement>::const_iterator ref_by_into(const std::string& name, bool fail = true) const;
  // Find which refinement has a from called 'name'
  std::vector<Refinement>::iterator ref_by_from(const std::string& name, bool fail = true);
  std::vector<Refinement>::const_iterator ref_by_from(const std::string& name, bool fail = true) const;
  // Make a unique refinement name for an into (by appending _2, etc, if needed)
  std::string unique_ref_name(const std::string& into) const;
  // Make a unique index name (by appending _2, etc, if needed)
  std::string unique_idx_name(const std::string& name) const;
  TensorShape exterior_shape(const std::string& name) const;

  std::shared_ptr<Block> SubBlock(size_t pos) const {
    auto it = stmts.begin();
    for (size_t i = 0; i < pos; i++) {
      ++it;
    }
    return Block::Downcast(*it);
  }
};

inline bool operator<(const StatementIt& lhs, const StatementIt& rhs) {  //
  return lhs->get() < rhs->get();
}

bool operator==(const BankDimension& lhs, const BankDimension& rhs);
bool operator==(const Index& lhs, const Index& rhs);
bool operator==(const Location& lhs, const Location& rhs);
bool operator!=(const Location& lhs, const Location& rhs);
bool operator<(const Location& lhs, const Location& rhs);

std::string to_string(const Location& loc);

std::ostream& operator<<(std::ostream& os, const Location& loc);
std::ostream& operator<<(std::ostream& os, const Index& idx);
std::ostream& operator<<(std::ostream& os, const Load& op);
std::ostream& operator<<(std::ostream& os, const Store& op);
std::ostream& operator<<(std::ostream& os, const Intrinsic& op);
std::ostream& operator<<(std::ostream& os, const Special& op);
std::ostream& operator<<(std::ostream& os, const Constant& op);
std::ostream& operator<<(std::ostream& os, const Refinement& ref);
std::ostream& operator<<(std::ostream& os, const Block& block);

std::shared_ptr<Block> FromProto(const proto::Block& block);
Affine FromProto(const proto::Affine& affine);
Location FromProto(const proto::Location& loc);
RefDir FromProto(const proto::Refinement::Dir& dir);
Tags FromProto(const google::protobuf::RepeatedPtrField<std::string>& pb_tags);

proto::Block IntoProto(const Block& block);
proto::Affine IntoProto(const Affine& affine);
proto::Location IntoProto(const Location& loc);

std::shared_ptr<Block> CloneBlock(const Block& orig, int depth = -1);
const Block* FindBlockByTag(const Block& block, const std::string& tag);
const Index* FindIndexByTag(const Block& block, const std::string& tag);

template <typename F>
void PreIterate(Block* block, const F& func) {
  auto it = block->stmts.begin();
  while (it != block->stmts.end()) {
    auto next = it;
    ++next;
    func(it);
    it = next;
  }
}

inline std::string to_string(const Block& block) {
  std::stringstream ss;
  ss << block;
  return ss.str();
}

}  // namespace stripe

namespace math {

inline std::ostream& operator<<(std::ostream& os, const stripe::Affine& affine) {
  os << affine.toString();
  return os;
}

}  // namespace math

}  // namespace tile
}  // namespace vertexai

namespace std {

template <>
struct hash<vertexai::tile::stripe::StatementIt> {
  std::size_t operator()(const vertexai::tile::stripe::StatementIt& it) const {
    return hash<vertexai::tile::stripe::Statement*>{}(it->get());
  }
};

}  // namespace std
