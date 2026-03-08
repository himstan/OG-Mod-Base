#pragma once

#include "lsp/protocol/common_types.h"
#include "lsp/state/workspace.h"

namespace lsp_handlers {
std::optional<json> references(Workspace& workspace, int id, json raw_params);
}  // namespace lsp_handlers
