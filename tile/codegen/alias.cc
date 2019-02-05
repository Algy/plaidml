// Copyright 2018, Intel Corporation

#include "tile/codegen/alias.h"

#include <boost/format.hpp>

#include "base/util/stream_container.h"
#include "base/util/throw.h"

namespace vertexai {
namespace tile {
namespace codegen {

using namespace stripe;  // NOLINT

namespace {

Affine UniqifyAffine(const Affine& orig, const std::string& prefix) {
  Affine ret;
  for (const auto& kvp : orig.getMap()) {
    if (kvp.first.empty()) {
      ret += kvp.second;
    } else {
      ret += Affine(prefix + kvp.first, kvp.second);
    }
  }
  return ret;
}

}  // namespace

std::ostream& operator<<(std::ostream& os, const AliasInfo& ai) {
  os << "(" << ai.base_name;
  os << ", " << ai.location;
  os << ", " << StreamContainer(ai.access);
  os << ", " << ai.shape;
  os << ")";
  return os;
}

std::ostream& operator<<(std::ostream& os, const Extent& extent) {
  os << "(" << extent.min << ", " << extent.max << ")";
  return os;
}

bool CheckOverlap(const std::vector<Extent>& ae, const std::vector<Extent>& be) {
  IVLOG(4, boost::format("  CheckOverlap: a: '%1%', b: '%2%'") % StreamContainer(ae) % StreamContainer(be));
  if (ae.size() != be.size()) {
    throw std::runtime_error("Incompatible extents");
  }
  bool ret = true;
  for (size_t i = 0; i < ae.size(); i++) {
    ret &= be[i].min <= ae[i].max;
    ret &= ae[i].min <= be[i].max;
  }
  return ret;
}

AliasType AliasInfo::Compare(const AliasInfo& ai, const AliasInfo& bi) {
  IVLOG(3, "AliasInfo::Compare> a: " << ai.base_name << ", b: " << bi.base_name);
  IVLOG(4, "  a: " << ai);
  IVLOG(4, "  b: " << bi);
  if (ai.base_name != bi.base_name) {
    IVLOG(3, "  Different base tensors");
    return AliasType::None;
  }
  if (ai.shape == bi.shape) {
    if (ai.location.unit.isConstant() && bi.location.unit.isConstant() && ai.location != bi.location) {
      IVLOG(3, boost::format("  Different banks, a: %1%, b: %2%") % ai.location % bi.location);
      return AliasType::None;
    }
    if (ai.access == bi.access) {
      IVLOG(3,
            boost::format("  Exact access, a: %1%, b: %2%") % StreamContainer(ai.access) % StreamContainer(bi.access));
      return AliasType::Exact;
    }
    if (!CheckOverlap(ai.extents, bi.extents)) {
      IVLOG(3, "  No overlap");
      return AliasType::None;
    }
  }
  // TODO: We could compute the convex box enclosing each refinement and then check each
  // dimension to see if there is a splitting plane, and if so, safely declare alias None,
  // but it's unclear that that will happen enough for us to care, so just always return
  // Partial which is conservative.
  IVLOG(3, "  Partial");
  return AliasType::Partial;
}

bool AliasInfo::IsBanked() const { return !!base_ref->bank_dim; }

AliasMap::AliasMap() : depth_(0) {}

AliasMap::AliasMap(const AliasMap& outer, stripe::Block* block) : depth_(outer.depth_ + 1) {
  // Make a prefix
  std::string prefix = str(boost::format("d%1%:") % depth_);
  // Make all inner alias data
  for (auto& ref : block->refs) {
    // Setup the place we are going to write to
    AliasInfo& info = info_[ref.into];
    // Check if it's a refinement or a new buffer
    if (ref.dir != RefDir::None) {
      // Get the state from the outer context, fail if not found
      auto it = outer.info_.find(ref.from);
      if (it == outer.info_.end()) {
        throw_with_trace(std::runtime_error(
            str(boost::format("AliasMap::AliasMap: invalid ref.from during aliasing computation: '%1%' (ref: '%2%')") %
                ref.from % ref)));
      }
      // Copy data across
      info.base_block = it->second.base_block;
      info.base_ref = it->second.base_ref;
      info.base_name = it->second.base_name;
      info.access = it->second.access;
      info.location = it->second.location;
      info.location.unit += ref.location.unit;
    } else {
      // New alloc, initialize from scratch
      info.base_block = block;
      info.base_ref = &ref;
      info.base_name = prefix + ref.into;
      info.access.resize(ref.access.size());
      info.location = ref.location;
    }
    if (info.access.size() != ref.access.size()) {
      throw_with_trace(std::runtime_error(
          str(boost::format("AliasMap::AliasMap: Mismatched access dimensions on refinement: %1% %2%") %
              info.base_name % ref.into)));
    }
    // Add in indexes from this block
    std::map<std::string, int64_t> min_idxs;
    std::map<std::string, int64_t> max_idxs;
    for (const auto& idx : block->idxs) {
      if (idx.affine.constant()) {
        min_idxs[idx.name] = idx.affine.constant();
        max_idxs[idx.name] = idx.affine.constant();
      } else {
        min_idxs[idx.name] = 0;
        max_idxs[idx.name] = idx.range - 1;
      }
    }
    info.extents.resize(ref.access.size());
    for (size_t i = 0; i < ref.access.size(); i++) {
      info.access[i] += UniqifyAffine(ref.access[i], prefix);
      int64_t min_extent = ref.access[i].eval(min_idxs);
      int64_t max_extent = ref.access[i].eval(max_idxs) + ref.interior_shape.dims[i].size - 1;
      info.extents[i] = Extent{min_extent, max_extent};
    }
    IVLOG(5, boost::format("Extents for '%1%' in '%2%': %3%") % ref.into % block->name % StreamContainer(info.extents));
    // Set shape
    info.shape = ref.interior_shape;
  }
}

std::unordered_map<std::string, size_t> AliasMap::RefUseCounts(const Block& block) const {
  // Compute statement use count of each buffer
  std::unordered_map<std::string, size_t> use_count;
  for (const auto& stmt : block.stmts) {
    std::set<std::string> buf_use;
    for (const auto& str : stmt->buffer_reads()) {
      buf_use.emplace(str);
    }
    for (const auto& str : stmt->buffer_writes()) {
      buf_use.emplace(str);
    }
    for (const auto& str : buf_use) {
      use_count[str]++;
    }
  }
  return use_count;
}

}  // namespace codegen
}  // namespace tile
}  // namespace vertexai
