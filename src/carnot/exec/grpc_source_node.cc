#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "src/carnot/exec/grpc_source_node.h"
#include "src/carnot/planpb/plan.pb.h"
namespace pl {
namespace carnot {
namespace exec {

using table_store::schema::RowBatch;

std::string GRPCSourceNode::DebugStringImpl() {
  return absl::Substitute("Exec::GRPCSourceNode: <id: $0, output: $1>", plan_node_->id(),
                          output_descriptor_->DebugString());
}

Status GRPCSourceNode::InitImpl(const plan::Operator& plan_node,
                                const table_store::schema::RowDescriptor& output_descriptor,
                                const std::vector<table_store::schema::RowDescriptor>&) {
  CHECK(plan_node.op_type() == planpb::OperatorType::GRPC_SOURCE_OPERATOR);
  const auto* source_plan_node = static_cast<const plan::GRPCSourceOperator*>(&plan_node);
  plan_node_ = std::make_unique<plan::GRPCSourceOperator>(*source_plan_node);
  output_descriptor_ = std::make_unique<table_store::schema::RowDescriptor>(output_descriptor);
  return Status::OK();
}
Status GRPCSourceNode::PrepareImpl(ExecState*) { return Status::OK(); }

Status GRPCSourceNode::OpenImpl(ExecState*) { return Status::OK(); }

Status GRPCSourceNode::CloseImpl(ExecState*) { return Status::OK(); }

Status GRPCSourceNode::GenerateNextImpl(ExecState* exec_state) {
  // NextBatchReady calls OptionallyPopRowBatch() to ensure a RowBatch is ready to go, if available.
  if (!NextBatchReady()) {
    return error::Internal("Error dequeuing RowBatch in GRPCSourceNode");
  }

  PL_ASSIGN_OR_RETURN(auto rb, RowBatch::FromProto(next_up_->row_batch()));
  PL_RETURN_IF_ERROR(SendRowBatchToChildren(exec_state, *rb));
  if (rb->eos()) {
    sent_eos_ = true;
  }
  next_up_ = nullptr;

  return Status::OK();
}

Status GRPCSourceNode::EnqueueRowBatch(std::unique_ptr<carnotpb::RowBatchRequest> row_batch) {
  if (!row_batch_queue_.enqueue(std::move(row_batch))) {
    return error::Internal("Failed to enqueue RowBatch");
  }
  return Status::OK();
}

bool GRPCSourceNode::HasBatchesRemaining() { return !sent_eos_; }

void GRPCSourceNode::OptionallyPopRowBatch() {
  if (next_up_ != nullptr) {
    return;
  }
  row_batch_queue_.try_dequeue(next_up_);
}

bool GRPCSourceNode::NextBatchReady() {
  OptionallyPopRowBatch();
  return next_up_ != nullptr;
}

}  // namespace exec
}  // namespace carnot
}  // namespace pl
