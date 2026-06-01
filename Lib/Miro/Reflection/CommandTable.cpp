#include "CommandTable.h"

namespace Miro
{

UnknownCommandError::UnknownCommandError(const std::string& commandToUse)
    : std::runtime_error("unknown command: " + commandToUse)
{
}

void CommandTable::registerHandler(const std::string& command,
                                   const RawHandler& handler)
{
    handlers[command] = handler;
}

bool CommandTable::has(std::string_view command) const
{
    return handlers.contains(std::string {command});
}

void CommandTable::dispatch(std::string_view command,
                            const JSON& payload,
                            const Reply& reply) const
{
    auto it = handlers.find(std::string {command});

    if (it == handlers.end())
        throw UnknownCommandError(std::string {command});

    it->second(payload, reply);
}

JSON CommandTable::dispatch(std::string_view command, const JSON& payload) const
{
    // Collect the reply synchronously. The slot is heap-owned so that a
    // handler which (incorrectly, for this form) replies on a later turn
    // writes into live memory rather than a dangling stack frame — that
    // late write is simply ignored. A synchronous handler fills it inline
    // before dispatch() returns; a synchronous throw propagates straight
    // through, preserving the exception type for callers that branch on it.
    struct Slot
    {
        bool replied = false;
        bool failed = false;
        JSON result;
        std::string error;
    };

    auto slot = std::make_shared<Slot>();

    dispatch(command,
             payload,
             [slot](const JSON& result, const std::string* error)
             {
                 if (slot->replied)
                     return;
                 slot->replied = true;
                 if (error != nullptr)
                 {
                     slot->failed = true;
                     slot->error = *error;
                 }
                 else
                 {
                     slot->result = result;
                 }
             });

    if (!slot->replied)
        throw std::runtime_error("command '" + std::string {command}
                                 + "' did not reply synchronously (it is "
                                   "asynchronous; this transport cannot wait)");

    if (slot->failed)
        throw std::runtime_error(slot->error);

    return slot->result;
}

} // namespace Miro
