#include <absl/strings/substitute.h>
#include <google/protobuf/text_format.h>

#include "src/common/base/base.h"
#include "src/common/exec/subprocess.h"
#include "src/common/fs/fs_wrapper.h"
#include "src/common/testing/testing.h"
#include "src/stirling/core/types.h"
#include "src/stirling/dynamic_tracer/dynamic_trace_connector.h"
#include "src/stirling/obj_tools/testdata/dummy_exe_fixture.h"
#include "src/stirling/testing/common.h"

#include "src/stirling/proto/stirling.pb.h"

constexpr std::string_view kClientPath =
    "src/stirling/socket_tracer/protocols/http2/testing/go_grpc_client/go_grpc_client_/"
    "go_grpc_client";
constexpr std::string_view kServerPath =
    "src/stirling/socket_tracer/protocols/http2/testing/go_grpc_server/go_grpc_server_/"
    "go_grpc_server";

namespace pl {
namespace stirling {

using ::google::protobuf::TextFormat;
using ::pl::stirling::testing::ColWrapperSizeIs;
using ::pl::stirling::testing::FindRecordsMatchingPID;
using ::testing::Each;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::SizeIs;
using ::testing::StrEq;

using LogicalProgram = ::pl::stirling::dynamic_tracing::ir::logical::TracepointDeployment;

// TODO(yzhao): Create test fixture that wraps the test binaries.
class GoHTTPDynamicTraceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    client_path_ = pl::testing::BazelBinTestFilePath(kClientPath).string();
    server_path_ = pl::testing::BazelBinTestFilePath(kServerPath).string();

    ASSERT_OK(fs::Exists(server_path_));
    ASSERT_OK(fs::Exists(client_path_));

    ASSERT_OK(s_.Start({server_path_}));

    // Give some time for the server to start up.
    sleep(2);

    std::string port_str;
    ASSERT_OK(s_.Stdout(&port_str));
    ASSERT_TRUE(absl::SimpleAtoi(port_str, &s_port_));
    ASSERT_NE(0, s_port_);
  }

  void TearDown() override {
    s_.Kill();
    EXPECT_EQ(9, s_.Wait()) << "Server should have been killed.";
  }

  void InitTestFixturesAndRunTestProgram(const std::string& text_pb) {
    CHECK(TextFormat::ParseFromString(text_pb, &logical_program_));

    logical_program_.mutable_deployment_spec()->set_path(server_path_);

    ASSERT_OK_AND_ASSIGN(connector_,
                         DynamicTraceConnector::Create("my_dynamic_source", &logical_program_));
    ASSERT_OK(connector_->Init());

    ASSERT_OK(c_.Start({client_path_, "-name=PixieLabs", "-count=200",
                        absl::StrCat("-address=localhost:", s_port_)}));
    EXPECT_EQ(0, c_.Wait()) << "Client should be killed";
  }

  std::vector<TaggedRecordBatch> GetRecords() {
    constexpr int kTableNum = 0;
    std::unique_ptr<StandaloneContext> ctx = std::make_unique<StandaloneContext>();
    std::unique_ptr<DataTable> data_table =
        std::make_unique<DataTable>(connector_->TableSchema(kTableNum));
    connector_->TransferData(ctx.get(), kTableNum, data_table.get());
    return data_table->ConsumeRecords();
  }

  std::string server_path_;
  std::string client_path_;

  SubProcess c_;
  SubProcess s_;
  int s_port_ = 0;

  LogicalProgram logical_program_;
  std::unique_ptr<SourceConnector> connector_;
};

constexpr char kGRPCTraceProgram[] = R"(
tracepoints {
  program {
    language: GOLANG
    outputs {
      name: "probe_WriteDataPadded_table"
      fields: "stream_id"
      fields: "end_stream"
      fields: "latency"
    }
    probes: {
      name: "probe_WriteDataPadded"
      tracepoint: {
        symbol: "golang.org/x/net/http2.(*Framer).WriteDataPadded"
        type: LOGICAL
      }
      args {
        id: "stream_id"
        expr: "streamID"
      }
      args {
        id: "end_stream"
        expr: "endStream"
      }
      function_latency { id: "latency" }
      output_actions {
        output_name: "probe_WriteDataPadded_table"
        variable_name: "stream_id"
        variable_name: "end_stream"
        variable_name: "latency"
      }
    }
  }
}
)";

constexpr char kReturnValueTraceProgram[] = R"(
tracepoints {
  program {
    language: GOLANG
    outputs {
      name: "probe_readFrameHeader"
      fields: "frame_header_valid"
    }
    probes: {
      name: "probe_StreamEnded"
      tracepoint: {
        symbol: "golang.org/x/net/http2.readFrameHeader"
        type: LOGICAL
      }
      ret_vals {
        id: "frame_header_valid"
        expr: "$0.valid"
      }
      output_actions {
        output_name: "probe_readFrameHeader"
        variable_name: "frame_header_valid"
      }
    }
  }
}
)";

TEST_F(GoHTTPDynamicTraceTest, TraceGolangHTTPClientAndServer) {
  ASSERT_NO_FATAL_FAILURE(InitTestFixturesAndRunTestProgram(kGRPCTraceProgram));
  std::vector<TaggedRecordBatch> tablets = GetRecords();

  ASSERT_FALSE(tablets.empty());

  {
    types::ColumnWrapperRecordBatch records =
        FindRecordsMatchingPID(tablets[0].records, /*index*/ 0, s_.child_pid());

    ASSERT_THAT(records, Each(ColWrapperSizeIs(200)));

    constexpr size_t kStreamIDIdx = 3;
    constexpr size_t kEndStreamIdx = 4;
    constexpr size_t kLatencyIdx = 5;

    EXPECT_EQ(records[kStreamIDIdx]->Get<types::Int64Value>(0).val, 1);
    EXPECT_EQ(records[kEndStreamIdx]->Get<types::BoolValue>(0).val, false);
    // 1000 is not particularly meaningful, it just states that we have a roughly correct
    // value.
    EXPECT_THAT(records[kLatencyIdx]->Get<types::Int64Value>(0).val, Gt(1000));
  }
}

TEST_F(GoHTTPDynamicTraceTest, TraceReturnValue) {
  ASSERT_NO_FATAL_FAILURE(InitTestFixturesAndRunTestProgram(kReturnValueTraceProgram));
  std::vector<TaggedRecordBatch> tablets = GetRecords();

  ASSERT_FALSE(tablets.empty());

  {
    types::ColumnWrapperRecordBatch records =
        FindRecordsMatchingPID(tablets[0].records, /*index*/ 0, s_.child_pid());

    ASSERT_THAT(records, Each(ColWrapperSizeIs(1600)));

    constexpr size_t kFrameHeaderValidIdx = 3;

    EXPECT_EQ(records[kFrameHeaderValidIdx]->Get<types::BoolValue>(0).val, true);
  }
}

class CPPDynamicTraceTest : public ::testing::Test {
 protected:
  void InitTestFixturesAndRunTestProgram(const std::string& text_pb) {
    CHECK(TextFormat::ParseFromString(text_pb, &logical_program_));

    logical_program_.mutable_deployment_spec()->set_path(dummy_exe_fixture_.Path());

    ASSERT_OK_AND_ASSIGN(connector_,
                         DynamicTraceConnector::Create("my_dynamic_source", &logical_program_));

    ASSERT_OK(connector_->Init());

    ASSERT_OK(dummy_exe_fixture_.Run());
  }

  std::vector<TaggedRecordBatch> GetRecords() {
    constexpr int kTableNum = 0;
    std::unique_ptr<StandaloneContext> ctx = std::make_unique<StandaloneContext>();
    std::unique_ptr<DataTable> data_table =
        std::make_unique<DataTable>(connector_->TableSchema(kTableNum));
    connector_->TransferData(ctx.get(), kTableNum, data_table.get());
    return data_table->ConsumeRecords();
  }

  // Need debug build to include the dwarf info.
  obj_tools::DummyExeFixture dummy_exe_fixture_;

  LogicalProgram logical_program_;
  std::unique_ptr<SourceConnector> connector_;
};

constexpr char kDummyExeTraceProgram[] = R"(
tracepoints {
  program {
    language: CPP
    outputs {
      name: "foo_bar_output"
      fields: "arg"
    }
    probes: {
      name: "probe_foo_bar"
      tracepoint: {
        symbol: "pl::testing::Foo::Bar"
        type: LOGICAL
      }
      args {
        id: "foo_bar_arg"
        expr: "i"
      }
      output_actions {
        output_name: "foo_bar_output"
        variable_name: "foo_bar_arg"
      }
    }
  }
}
)";

TEST_F(CPPDynamicTraceTest, DISABLED_TraceDummyExe) {
  // TODO(yzhao): This does not work yet.
  ASSERT_NO_FATAL_FAILURE(InitTestFixturesAndRunTestProgram(kDummyExeTraceProgram));
  std::vector<TaggedRecordBatch> tablets = GetRecords();
  PL_UNUSED(tablets);
}

}  // namespace stirling
}  // namespace pl