// Copyright 2018, Intel Corporation

#pragma once

#include "tile/codegen/codegen.pb.h"
#include "tile/stripe/stripe.h"

namespace vertexai {
namespace tile {
namespace codegen {

void AutotilePass(stripe::Block* block, const proto::AutotilePass& options);

void PartitionComputePass(stripe::Block* root, const proto::PartitionPass& options);

}  // namespace codegen
}  // namespace tile
}  // namespace vertexai
