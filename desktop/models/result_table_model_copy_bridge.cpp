#include "bridge/cell_transport.h"
#include "bridge/rust_text.h"
#include "models/result_table_model.h"

namespace choscordb {
namespace {
using bridge_detail::fromRust;

QString copyError(const CopyResultDto& result) {
    const auto code = fromRust(result.error);
    if (code == QLatin1String("invalid_resolution"))
        return QObject::tr("A loaded copy value is invalid.");
    if (code == QLatin1String("wrong_resolution_type"))
        return QObject::tr("A loaded copy value has the wrong type.");
    if (code == QLatin1String("incomplete_resolution"))
        return QObject::tr("A loaded copy value is incomplete.");
    if (code == QLatin1String("unavailable"))
        return QObject::tr("Cannot copy unavailable %1 value: %2")
            .arg(fromRust(result.database_type), fromRust(result.reason));
    if (code == QLatin1String("deferred"))
        return QObject::tr("Large values cannot be copied from the grid. Export the result to "
                           "copy the complete value.");
    if (code == QLatin1String("limit"))
        return QObject::tr("Copied selection exceeds the clipboard size limit.");
    return QObject::tr("A copied value is invalid.");
}
} // namespace

ResultTableModel::CopyEvaluation ResultTableModel::evaluateCopy(CopySnapshot snapshot) {
    CopyRequestDto request;
    request.byte_budget = snapshot.byteBudget;
    request.valid_unicode = true;
    for (auto& row : snapshot.rows) {
        CopyRowDto rowDto;
        for (auto& selected : row) {
            CopyCellDto cellDto;
            cellDto.selected = selected.has_value();
            if (selected) {
                cellDto.original =
                    bridge_detail::cellDto(selected->original, false, &request.valid_unicode);
                cellDto.has_resolved = selected->resolved.has_value();
                if (selected->resolved)
                    cellDto.resolved =
                        bridge_detail::cellDto(*selected->resolved, false, &request.valid_unicode);
                cellDto.inserted_omitted = selected->insertedOmitted;
            }
            rowDto.cells.push_back(std::move(cellDto));
        }
        request.rows.push_back(std::move(rowDto));
    }
    for (auto& resolution : snapshot.resolutions) {
        CopyResolutionDto resolutionDto;
        resolutionDto.has_original = resolution.original.has_value();
        if (resolution.original)
            resolutionDto.original =
                bridge_detail::cellDto(*resolution.original, false, &request.valid_unicode);
        resolutionDto.resolved =
            bridge_detail::cellDto(resolution.resolved, false, &request.valid_unicode);
        request.resolutions.push_back(std::move(resolutionDto));
    }
    const auto result = render_copy_tsv_policy(std::move(request));
    if (!result.error.empty())
        return {.text = {}, .error = copyError(result)};
    return {.text = fromRust(result.text), .error = {}};
}
} // namespace choscordb
