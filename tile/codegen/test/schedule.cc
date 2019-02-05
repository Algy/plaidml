// Copyright 2018 Intel Corporation.

#include <gtest/gtest.h>

#include "testing/matchers.h"
#include "tile/codegen/driver.h"
#include "tile/codegen/schedule.h"
#include "tile/lang/gen_stripe.h"
#include "tile/lib/tests.h"
#include "tile/stripe/stripe.h"
#include "tile/stripe/stripe.pb.h"

using ::testing::EqualsProtoText;

namespace vertexai {
namespace tile {
namespace codegen {
namespace test {

template <typename P>
P ParseProtoText(const std::string& txt) {
  P proto;
  google::protobuf::TextFormat::ParseFromString(txt, &proto);
  return proto;
}

class ScheduleTest : public ::testing::Test {
 public:
  void SetUp() override {
    SetUpBlock();
    SetUpOptions();
    main_ = block_->SubBlock(0);
  }

  virtual void SetUpBlock() {
    block_ = stripe::FromProto(ParseProtoText<stripe::proto::Block>(R"(
      name: "program" loc {unit {}}
      refs [{into: "i1" loc {name: "RAM" unit {}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
            {into: "i2" loc {name: "RAM" unit {}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
            {into: "o1" loc {name: "RAM" unit {}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}}]
      stmts [{
        tags: ["main"] block {
          name: "main" loc {unit {}}
          refs [{from: "i1" into: "i1" dir: In loc {name: "RAM" unit {}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
                {from: "i2" into: "i2" dir: In loc {name: "RAM" unit {}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
                {from: "o1" into: "o1" dir: Out loc {name: "RAM" unit {}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}}]
        }
      }]
    )"));
  }

  virtual void SetUpOptions() {
    options_ = ParseProtoText<proto::SchedulePass>(R"(
      reqs: ["main"],
      mem_loc: { name: "CACHE" },
      mem_KiB: 1024,
      alignment: 16,
      xfer_loc: { name: "DMA" }
    )");
  }

  void AddTmpRefinement(const char* name, const TensorDimension& dim) {
    main_->refs.emplace_back(stripe::Refinement{
        stripe::RefDir::None,                   // dir
        "",                                     // from
        name,                                   // into
        {stripe::Affine{}},                     // access
        TensorShape(DataType::FLOAT32, {dim}),  // shape
        "",                                     // agg_op
        stripe::Location{"RAM"},                // location
    });
  }

 protected:
  std::shared_ptr<stripe::Block> block_;
  std::shared_ptr<stripe::Block> main_;
  proto::SchedulePass options_;
};

TEST_F(ScheduleTest, EmptyMain) {
  SchedulePass(block_.get(), options_);
  EXPECT_THAT(IntoProto(*block_), EqualsProtoText(R"(
    name: "program"
    loc { unit { } }
    refs [{into: "i1" loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
          {into: "i2" loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
          {into: "o1" loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}}]
    stmts [{
      tags: ["main"] block {
        name: "main" loc {unit {}}
        refs [{dir: In from: "i1" into: "i1" loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
              {dir: In from: "i2" into: "i2" loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
              {dir: Out from: "o1" into: "o1" loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}}]
      }
    }]
  )"));
}

TEST_F(ScheduleTest, CachesIO) {
  main_->stmts.emplace_back(stripe::FromProto(ParseProtoText<stripe::proto::Block>(R"(
    name: "sub_block_1" loc {unit {}}
    refs [{from: "i1" into: "i1" dir: In loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
          {from: "i2" into: "i2" dir: In loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
          {from: "o1" into: "o1" dir: Out loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}}]
  )")));
  SchedulePass(block_.get(), options_);
  EXPECT_THAT(IntoProto(*block_), EqualsProtoText(R"(
    name: "program"
    loc { unit { } }
    refs [{into: "i1" loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
          {into: "i2" loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
          {into: "o1" loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}}]
    stmts [{
      tags: ["main"] block {
        name: "main" loc {unit {}}
        refs [{from: "i1" into: "i1" dir: In loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
              {into: "i1^0" offset: 128 loc {name: "CACHE" unit {}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
              {from: "i2" into: "i2" dir: In loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
              {into: "i2^0" offset: 64 loc {name: "CACHE" unit {}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
              {from: "o1" into: "o1" dir: Out loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
              {into: "o1^0" loc {name: "CACHE" unit {}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}}]
        stmts [{
          block {
            name: "swap_in_i2^0" loc {name: "DMA" unit {}}
            idxs [{name: "i0" range: 16 affine {}}]
            refs [{from: "i2" into: "src" dir: In access [{terms [{key: "i0" value: 1}]}] loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:1 stride:1}}},
                  {from: "i2^0" into: "dst" dir: Out access [{terms [{key: "i0" value: 1}]}] loc {name: "CACHE" unit{}} shape {type: FLOAT32 dims: {size:1 stride:1}}}]
            stmts [{load: {from: "src" into: "$X"}}, {store: {from: "$X" into: "dst"}}]
          }
        }, {
          block {
            name: "swap_in_i1^0" loc {name: "DMA" unit {}}
            idxs [{name: "i0" range: 16 affine {}}]
            refs [{from: "i1" into: "src" dir: In access [{terms [{key: "i0" value: 1}]}] loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:1 stride:1}}},
                  {from: "i1^0" into: "dst" dir: Out access [{terms [{key: "i0" value: 1}]}] loc {name: "CACHE" unit{}} shape {type: FLOAT32 dims: {size:1 stride:1}}}]
            stmts [{load: {from: "src" into: "$X"}}, {store: {from: "$X" into: "dst"}}]
          }
        }, {
          block {
            name: "sub_block_1" loc {unit {}}
            refs [{from: "i1^0" into: "i1" dir: In loc {name: "CACHE" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
                  {from: "i2^0" into: "i2" dir: In loc {name: "CACHE" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
                  {from: "o1^0" into: "o1" dir: Out loc {name: "CACHE" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}}]
          }
          deps: [0, 1]
        }, {
          block {
            name: "swap_out_o1^0" loc {name: "DMA" unit {}}
            idxs [{name: "i0" range: 16 affine {}}]
            refs [{from: "o1^0" into: "src" dir: In access [{terms [{key: "i0" value: 1}]}] loc {name: "CACHE" unit{}} shape {type: FLOAT32 dims: {size:1 stride:1}}},
                  {from: "o1" into: "dst" dir: Out access [{terms [{key: "i0" value: 1}]}] loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:1 stride:1}}}]
            stmts [{load: {from: "src" into: "$X"}}, {store: {from: "$X" into: "dst"}}]
          }
          deps: [2]
        }]
      }
    }]
  )"));
}

TEST_F(ScheduleTest, UsesTmps) {
  main_->stmts.emplace_back(stripe::FromProto(ParseProtoText<stripe::proto::Block>(R"(
    name: "sub_block_1" loc {unit {}}
    refs [{from: "i1" into: "i1" dir: In loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
          {from: "i2" into: "i2" dir: In loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
          {from: "t1" into: "t1" dir: Out loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}}]
  )")));

  main_->stmts.emplace_back(stripe::FromProto(ParseProtoText<stripe::proto::Block>(R"(
    name: "sub_block_2" loc {unit {}}
    refs [{from: "t1" into: "t1" dir: In loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
          {from: "i2" into: "i2" dir: In loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
          {from: "o1" into: "o1" dir: Out loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}}]
  )")));

  AddTmpRefinement("t1", TensorDimension{1, 16});

  SchedulePass(block_.get(), options_);

  EXPECT_THAT(IntoProto(*block_), EqualsProtoText(R"(
    name: "program"
    loc { unit { } }
    refs [{into: "i1" loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
          {into: "i2" loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
          {into: "o1" loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}}]
    stmts [{
      tags: ["main"] block {
        name: "main" loc {unit {}}
        refs [{from: "i1" into: "i1" dir: In loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
              {into: "i1^0" offset: 64 loc {name: "CACHE" unit {}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
              {from: "i2" into: "i2" dir: In loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
              {into: "i2^0" offset: 128 loc {name: "CACHE" unit {}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
              {from: "o1" into: "o1" dir: Out loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
              {into: "o1^0" offset: 64 loc {name: "CACHE" unit {}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
              {into: "t1" loc {name: "RAM" unit {}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
              {into: "t1^0" loc {name: "CACHE" unit {}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}}]
        stmts [{
          block {
            name: "swap_in_i1^0" loc {name: "DMA" unit {}}
            idxs [{name: "i0" range: 16 affine {}}]
            refs [{from: "i1" into: "src" dir: In access [{terms [{key: "i0" value: 1}]}] loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:1 stride:1}}},
                  {from: "i1^0" into: "dst" dir: Out access [{terms [{key: "i0" value: 1}]}] loc {name: "CACHE" unit{}} shape {type: FLOAT32 dims: {size:1 stride:1}}}]
            stmts [{load: {from: "src" into: "$X"}}, {store: {from: "$X" into: "dst"}}]
          }
        }, {
          block {
            name: "swap_in_i2^0" loc {name: "DMA" unit {}}
            idxs [{name: "i0" range: 16 affine {}}]
            refs [{from: "i2" into: "src" dir: In access [{terms [{key: "i0" value: 1}]}] loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:1 stride:1}}},
                  {from: "i2^0" into: "dst" dir: Out access [{terms [{key: "i0" value: 1}]}] loc {name: "CACHE" unit{}} shape {type: FLOAT32 dims: {size:1 stride:1}}}]
            stmts [{load: {from: "src" into: "$X"}}, {store: {from: "$X" into: "dst"}}]
          }
        }, {
          block {
            name: "sub_block_1" loc {unit {}}
            refs [{from: "i1^0" into: "i1" dir: In loc {name: "CACHE" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
                  {from: "i2^0" into: "i2" dir: In loc {name: "CACHE" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
                  {from: "t1^0" into: "t1" dir: Out loc {name: "CACHE" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}}]
          }
          deps: [0, 1]
        }, {
          block {
            name: "sub_block_2" loc {unit {}}
            refs [{from: "t1^0" into: "t1" dir: In loc {name: "CACHE" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
                  {from: "i2^0" into: "i2" dir: In loc {name: "CACHE" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}},
                  {from: "o1^0" into: "o1" dir: Out loc {name: "CACHE" unit{}} shape {type: FLOAT32 dims: {size:16 stride:1}} access {}}]
          }
          deps: [2]
        }, {
          block {
            name: "swap_out_o1^0" loc {name: "DMA" unit {}}
            idxs [{name: "i0" range: 16 affine {}}]
            refs [{from: "o1^0" into: "src" dir: In access [{terms [{key: "i0" value: 1}]}] loc {name: "CACHE" unit{}} shape {type: FLOAT32 dims: {size:1 stride:1}}},
                  {from: "o1" into: "dst" dir: Out access [{terms [{key: "i0" value: 1}]}] loc {name: "RAM" unit{}} shape {type: FLOAT32 dims: {size:1 stride:1}}}]
            stmts [{load: {from: "src" into: "$X"}}, {store: {from: "$X" into: "dst"}}]
          }
          deps: [3]
        }]
      }
    }]
  )"));
}

TEST(Schedule, Basic) {
  auto cfg_tmpl = R"(
    passes: { name: "loc_prog" locate_memory: { reqs: ["program"] loc: { name: "DRAM" } } }
    passes: { name: "loc_main" locate_memory: { reqs: ["main"] loc: { name: "DRAM" } } }
    passes: { name: "loc_proc"
      locate_block: {
        reqs: ["kernel"]
        loc: {
          name: "PROC"
          unit: {
            terms: { key: "#bank" value: %2% }
            terms: { key: "#proc" value: 1 }
          }
        }
      }
    }
    passes: { name: "partition_memory"
      partition_memory: {
        reqs: ["kernel"]
        num_parts: %1%
        set_tags: ["bank_part"]
        idx_tag: "bank"
      }
    }
    passes: { name: "unroll_bank_parts"
      unroll: {
        reqs: ["bank_part"]
        expand_idx: "#bank"
        part_name: "bank"
        make_views: true
      }
    }
    passes: { name: "fit_into_mem"
      autotile: {
        reqs: ["kernel"]
        outer_set: ["fit_part"]
        skip_1d: true
        only_po2: true
        max_total_size : %3%
        input_cost: 1.0
        output_cost: 1.0
        copy_tags: true
      }
    }
    passes: { name: "unroll_fit_parts"
      unroll: {
        reqs: ["fit_part"]
        part_name: "part"
        make_views: true
      }
    }
    passes: { name: "partition_compute"
      partition_compute: {
        reqs: ["kernel"]
        num_parts: %2%
        set_tags: ["compute_part"]
        idx_tag: "proc"
      }
    }
    passes: { name: "unroll_compute_parts"
      unroll: {
        reqs: ["compute_part"]
        expand_idx: "#proc"
        part_name: "proc"
      }
    }
    passes: { name: "schedule"
      schedule: {
        reqs: ["main"]
        mem_loc: { name: "SRAM" }
        mem_KiB: %4%
        alignment: 16
        xfer_loc: { name: "DMA" }
      }
    }
    passes: { name: "prune_refs" prune_refs: { reqs: ["program"] } }
  )";
  size_t num_banks = 2;
  size_t num_procs = 4;
  size_t procs_per_bank = num_procs / num_banks;
  size_t bank_size_KiB = 192;
  size_t bank_size = bank_size_KiB * 1024;
  auto cfg = ParseProtoText<proto::Config>(
      str(boost::format(cfg_tmpl) % num_banks % procs_per_bank % bank_size % bank_size_KiB));
  auto dbg_dir = std::getenv("DBG_DIR");
  OptimizeOptions options;
  if (dbg_dir) {
    options.dump_passes = true;
    options.dbg_dir = dbg_dir;
    IVLOG(1, "Writing passes to: " << dbg_dir);
  }
  auto tests = lib::InternalTests();
  const auto runinfo = tests.at("$layer_test2");
  auto program = GenerateStripe(runinfo);
  Optimize(program.get(), cfg, options);
}

}  // namespace test
}  // namespace codegen
}  // namespace tile
}  // namespace vertexai
