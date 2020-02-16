#ifndef STOMP_BASE_TRANSPORT_H
#define STOMP_BASE_TRANSPORT_H

#include <optional>
#include <string>
#include <vector>
#include <map>
#include <memory>

#include "publisher.h"
#include "listener.h"
#include "frame.h"

namespace stomp {
  class BaseTransport : public Publisher {
  protected:
    // recvbuf
    bool running_ {false};
    // blocking_
    bool connected_ {false};
    bool connectionError_ {false};
    std::map<std::string,std::string> receipts_ {};
    HostAndPortPtr currentHostAndPort_ {};
    std::optional<std::string> disconnectReceipt_ {};
    bool notifiedOnDisconnect_ {false};
    // createThreadFc_
    // listenersChangeCondition_
    // receiverThreadExitCondition_
    // receiverThreadExited_
    // sendWaitCondition_
    // connectWaitCondition_
    bool autoDecode_ {true};
    std::string encoding_ {};
  public:
    BaseTransport(bool autoDecode = true, std::string encoding = "utf8") :
      autoDecode_ {autoDecode}, encoding_ {encoding} {}
    // Override for thread creation. Use an alternate threading library by
    // setting this to a function with a single argument (which is the receiver loop callback).
    // The thread which is returned should be started (ready to run)
    // virtual void overrideThreading(createThreadFc)

    // Start the connection. This should be called after all
    // listeners have been registered. If this method is not called,
    // no frames will be received by the connection and no SSL/TLS
    // handshake will occur.
    virtual void start() {
      running_ = true;
      this->attemptConnection();
      // this->createThreadFc(receiverLoop)
      this->notify(std::make_shared<Frame>(FRAME_CONNECTING));
    }
    // Stop the connection. Performs a clean shutdown by waiting for the
    // receiver thread to exit.
    // virtual void stop() {}

    virtual bool isConnected() { return connected_; }
    virtual void setConnected(bool connected) {
      // TODO connect wait semaphore
      connected_ = connected;
    }
    virtual void setReceipt(std::string receiptId, std::optional<std::string> value) {
      if (value) {
        receipts_[receiptId] = value.value();
      } else {
        receipts_.erase(receiptId);
      }
    }
    // Set a named listener to use with this connection.
    virtual void setListener(std::string name, ConnectionListenerPtr listener) {
      // TODO listener change semaphore
      listeners_[name] = listener;
    }
    // Remove a listener according to the specified name.
    virtual void removeListener(std::string name) {
      // TODO listener change semaphore
      listeners_.erase(name);
    }
    // Return the named listener.
    virtual ConnectionListenerPtr getListener(std::string name) {
      return listeners_[name];
    }
    virtual void processFrame(FramePtr frame) {
      std::string frameType = frame->getCmd();
      if (frameType == FRAME_MESSAGE) {
        FramePtr beforeFrame = std::make_shared<Frame>(FRAME_BEFORE_MESSAGE, frame->getHeaders(), frame->getBody());
        this->notify(beforeFrame);
        frame->setHeaders(beforeFrame->getHeaders());
        frame->setBody(beforeFrame->getBody());
      }
      if (frameType == FRAME_MESSAGE || frameType == FRAME_CONNECTED || frameType == FRAME_RECEIPT || frameType == FRAME_ERROR || frameType == FRAME_HEARTBEAT) {
        this->notify(frame);
      }
      // TODO call notify
    }
    // Utility function for notifying listeners of incoming and outgoing messages.
    virtual void notify(FramePtr frame) {
      std::string frameType = frame->getCmd();
      if (frameType == FRAME_RECEIPT) {
        std::string receipt = frame->getReceiptIdHeader();
        std::string receiptValue = receipts_[receipt];
        // TODO use semaphore
        this->setReceipt(receipt, std::nullopt);
        if (receiptValue == FRAME_DISCONNECT) {
          this->setConnected(false);
          if (disconnectReceipt_ && receipt == disconnectReceipt_.value()) {
            this->disconnectSocket();
          }
          disconnectReceipt_ = std::nullopt;
        }
      } else if (frameType == FRAME_CONNECTED) {
        this->setConnected(true);
      } else if (frameType == FRAME_DISCONNECTED) {
        this->setConnected(false);
      }
      for (auto& [name, listener] : listeners_) {
        listener->notify(frame, currentHostAndPort_);
      }
      if (frameType == FRAME_ERROR && !connected_) {
        // TODO use connect semaphore
        connectionError_ = true;
      }
    }
    // Convert a frame object to a frame string and transmit to the server.
    virtual void transmit(FramePtr frame) {
      for (auto& [name, listener] : listeners_) {
        listener->onSend(frame);
      }
      if (frame->getCmd() == FRAME_DISCONNECT && frame->hasReceiptHeader()) {
        disconnectReceipt_ = frame->getReceiptHeader();
      }
      std::string content {frame->getContents()};
      this->send(content);
    }
    // Send an encoded frame over this transport (to be implemented in subclasses).
    virtual void send(std::string content) = 0;
    // Receive a chunk of data (to be implemented in subclasses).
    virtual std::string receive() = 0;
    // Cleanup the transport (to be implemented in subclasses).
    virtual void cleanup() = 0;
    // Attempt to establish a connection.
    virtual void attemptConnection() = 0;
    // Disconnect the socket.
    virtual void disconnectSocket() = 0;
    // Wait until we've established a connection with the server.
    virtual void waitForConnection(double timeout = 0) {
      // TODO implement timeout
    }
    // Main loop listening for incoming data.
    virtual void receiverLoop() {
      while (running_) {
        // TODO
      }
      this->notify(std::make_shared<Frame>(FRAME_RECEIVER_LOOP_COMPLETED));
      if (!notifiedOnDisconnect_) {
        this->notify(std::make_shared<Frame>(FRAME_DISCONNECTED));
      }
    }
    // Read the next frame(s) from the socket.
    virtual std::string read() {
      if (running_) {
        std::string content {this->receive()};
      }
      // TODO read from connection
      return "";
    }
  };
  using TransportPtr = std::shared_ptr<BaseTransport>;
}

#endif