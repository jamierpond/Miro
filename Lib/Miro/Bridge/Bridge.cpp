#include "Bridge.h"

namespace Miro
{

void Bridge::dispatch(std::string_view command,
                      const JSON& payloadToUse,
                      const CommandTable::Reply& reply) const
{
    commands.dispatch(command, payloadToUse, reply);
}

JSON Bridge::dispatch(std::string_view command, const JSON& payloadToUse) const
{
    return commands.dispatch(command, payloadToUse);
}

void Bridge::emitJson(const std::string& eventToUse, const JSON& payloadToUse)
{
    event = eventToUse;
    payload = &payloadToUse;
    onEmit.trigger();
}

} // namespace Miro
