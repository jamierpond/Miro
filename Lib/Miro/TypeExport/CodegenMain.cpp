#include "CodegenMain.h"

#include "Format.h"

#include <iostream>
#include <string_view>

namespace Miro::TypeExport
{

namespace
{

void usage(const char* exeName)
{
    std::cerr << "Usage: " << exeName
              << " --out <dir> [--name <basename>] [--format <name>]...\n"
              << "  --out <dir>       Directory to write generated files into\n"
              << "  --name <basename> Output filename stem (default: schema)\n"
              << "  --format <name>   Repeatable; defaults to all known formats\n"
              << "Known formats:";
    for (auto& fmt: Detail::formatRegistry())
        std::cerr << " " << fmt.name;
    std::cerr << "\n";
}

bool isFormatRequested(const Vector<std::string>& requested,
                       std::string_view formatName)
{
    return requested.empty() || requested.contains(std::string {formatName});
}

} // namespace

CodegenArgs parseCodegenArgs(int argc, char** argv)
{
    auto args = CodegenArgs {};

    for (auto i = 1; i < argc; ++i)
    {
        auto arg = std::string_view {argv[i]};

        if (arg == "--out" && i + 1 < argc)
            args.outDir = argv[++i];
        else if (arg == "--name" && i + 1 < argc)
            args.baseName = argv[++i];
        else if (arg == "--format" && i + 1 < argc)
            args.requestedFormats.addIfNotThere(argv[++i]);
        else
        {
            usage(argv[0]);
            return args;
        }
    }

    args.valid = !args.outDir.empty();
    if (!args.valid)
        usage(argv[0]);
    return args;
}

Vector<CommandExport::CommandEntry> toCommandEntries(
    const Vector<Miro::Detail::DescribeReflector::CommandRecord>& records)
{
    auto entries = Vector<CommandExport::CommandEntry> {};
    entries.reserve(records.size());

    for (auto& r: records)
    {
        // Built in one initializer rather than field by field: the
        // piecewise form has GCC reporting the reads of `r` against the
        // half-written `entry` (-Wmaybe-uninitialized). thunk is left
        // empty either way — the codegen path doesn't dispatch.
        entries.add(CommandExport::CommandEntry {
            .name = r.name,
            .hasRequest = bool(r.req),
            .requestTypeName = r.req.name,
            .requestQualifiedName = r.req.qualifiedName,
            .hasResponse = bool(r.res),
            .responseTypeName = r.res.name,
            .responseQualifiedName = r.res.qualifiedName,
            .thunk = {},
        });
    }

    return entries;
}

Vector<EventInfo>
    toEventInfos(const Vector<Miro::Detail::DescribeReflector::EventRecord>& records)
{
    auto infos = Vector<EventInfo> {};
    infos.reserve(records.size());

    for (auto& r: records)
    {
        auto info = EventInfo {};
        info.name = r.name;
        info.payloadTypeName = r.payload.name;
        info.payloadQualifiedName = r.payload.qualifiedName;
        info.defaultPayloadJson = r.defaultPayloadJson;
        info.isKeyed = r.isKeyed;
        info.collectionField = r.collectionField;
        info.keyField = r.keyField;
        infos.add(std::move(info));
    }

    return infos;
}

Vector<EmittedFile> runFormatsToMemory(const Context& ctx,
                                       const Vector<std::string>& requestedFormats)
{
    auto out = Vector<EmittedFile> {};

    for (auto& fmt: Detail::formatRegistry())
    {
        if (!isFormatRequested(requestedFormats, fmt.name))
            continue;

        auto file = EmittedFile {};
        file.filename = std::string {ctx.baseName} + fmt.extension;
        file.contents = fmt.generate(ctx);
        out.add(std::move(file));
    }

    return out;
}

} // namespace Miro::TypeExport
