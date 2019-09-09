#pragma once

#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/descriptor_database.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/message.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/common/base/base.h"

namespace pl {
namespace grpc {

struct MethodInputOutput {
  std::unique_ptr<google::protobuf::Message> input;
  std::unique_ptr<google::protobuf::Message> output;
};

/**
 * @brief Indexes services and the descriptors of their methods' input and output protobuf messages.
 */
class ServiceDescriptorDatabase {
 public:
  explicit ServiceDescriptorDatabase(google::protobuf::FileDescriptorSet fdset);

  /**
   * @brief Returns empty instances of the input and output type of the method specified by the
   * input method path.
   *
   * @param method_path A dot-separated name including the service name.
   */
  MethodInputOutput GetMethodInputOutput(const std::string& method_path);

  /**
   * @brief Returns an empty instance of the message specified by the input path.
   *
   * @param message_path A dot-separated name.
   */
  std::unique_ptr<google::protobuf::Message> GetMessage(const std::string& message_path);

  /**
   * @brief Returns all services in the descriptor database.
   *
   * Note that this function was added for message type inference, when the message type
   * of a message is not known. This function should not be necessary in code that
   * knows the message type of a message.
   *
   * @return vector of service descriptor protos.
   */
  std::vector<google::protobuf::ServiceDescriptorProto> AllServices();

 private:
  google::protobuf::SimpleDescriptorDatabase desc_db_;
  google::protobuf::DescriptorPool desc_pool_;
  google::protobuf::DynamicMessageFactory message_factory_;
};

/**
 * @brief Attempts to parse an instance of a protobuf message as the provided message type.
 *
 * @param message_type_name protobuf message type (e.g. hipstershop.GetCartRequest).
 * @param message a protobuf message in the wire format.
 * @return A unique_ptr to the decoded message if it was parseable, nullptr otherwise.
 *         An error is returned if the message_type_name is unknown.
 */
StatusOr<std::unique_ptr<google::protobuf::Message>> ParseAs(
    ServiceDescriptorDatabase* desc_db, const std::string& message_type_name,
    const std::string& message, bool allow_unknown_fields = false,
    bool allow_repeated_opt_fields = false);

// TODO(yzhao): Benchmark dynamic message parsing.

}  // namespace grpc
}  // namespace pl
