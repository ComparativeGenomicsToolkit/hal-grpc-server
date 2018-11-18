#include <grpcpp/grpcpp.h>
#include <grpc/support/log.h>

#include "halBlockViz.grpc.pb.h"

using namespace hal;
using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCompletionQueue;
using grpc::Status;

class BlockVizServer final {
 public:
  ~BlockVizServer() {
    _server->Shutdown();
    _cq->Shutdown();
  }

  void run() {
    std::string server_address("0.0.0.0:50051");

    ServerBuilder builder;
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    // Register "_service" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *asynchronous* service.
    builder.RegisterService(&_service);
    // Get hold of the completion queue used for the asynchronous communication
    // with the gRPC runtime.
    _cq = builder.AddCompletionQueue();
    // Finally assemble the server.
    _server = builder.BuildAndStart();
    std::cout << "Server listening on " << server_address << std::endl;

    // Proceed to the server's main loop.
    HandleRpcs();
  }

 private:
  class CallData {
   public:
    CallData(BlockViz::AsyncService* service, ServerCompletionQueue* cq)
        : _service(service), _cq(cq), _responder(&_ctx), _status(CREATE) {
      Proceed();
    }

    void Proceed() {
      if (_status == CREATE) {
        _status = PROCESS;
      } else if (_status == PROCESS) {
        new CallData(_service, _cq);
        _status = FINISH;
        _responder.Finish(_reply, Status::OK, this);
      } else {
        GPR_ASSERT(_status == FINISH);
        delete this;
      }
    }

   private:
    BlockViz::AsyncService* _service;
    ServerCompletionQueue* _cq;
    ServerContext _ctx;

    BlockRequest _request;
    BlockResults _reply;

    ServerAsyncResponseWriter<BlockResults> _responder;

    enum CallStatus { CREATE, PROCESS, FINISH };
    CallStatus _status;  // The current serving state.
  };

  // This can be run in multiple threads if needed.
  void HandleRpcs() {
    // Spawn a new CallData instance to serve new clients.
    new CallData(&_service, _cq.get());
    void* tag;  // uniquely identifies a request.
    bool ok;
    while (true) {
      // Block waiting to read the next event from the completion queue. The
      // event is uniquely identified by its tag, which in this case is the
      // memory address of a CallData instance.
      // The return value of Next should always be checked. This return value
      // tells us whether there is any kind of event or _cq is shutting down.
      GPR_ASSERT(_cq->Next(&tag, &ok));
      GPR_ASSERT(ok);
      static_cast<CallData*>(tag)->Proceed();
    }
  }

  std::unique_ptr<ServerCompletionQueue> _cq;
  BlockViz::AsyncService _service;
  std::unique_ptr<Server> _server;
};

int main(int argc, char* argv[]) {
  BlockVizServer server;
  server.run();

  return 0;
}
