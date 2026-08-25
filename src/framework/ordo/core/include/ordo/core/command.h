#pragma once

namespace ordo::core {

class AppKernel;

class ICommand {
public:
    virtual ~ICommand() = default;
};

// A transaction object created fresh per dispatched EventT (see
// AppKernel::registerCommand). Subclasses implement execute() to read the event
// payload and act on the kernel's agents/dispatcher.
template <typename EventT>
class Command : public ICommand {
public:
    virtual void execute(AppKernel& kernel, const EventT& event) = 0;
};

}  // namespace ordo::core
