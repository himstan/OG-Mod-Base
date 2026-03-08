#include "go_to.h"

#include "common/util/ast_util.h"
#include "lsp/lsp_util.h"

namespace lsp_handlers {
std::optional<json> go_to_definition(Workspace& workspace, int /*id*/, json raw_params) {
  auto params = raw_params.get<LSPSpec::TextDocumentPositionParams>();
  const auto file_type = workspace.determine_filetype_from_uri(params.m_textDocument.m_uri);

  json locations = json::array();

  if (file_type == Workspace::FileType::OpenGOALIR) {
    auto maybe_tracked_file = workspace.get_tracked_ir_file(params.m_textDocument.m_uri);
    if (!maybe_tracked_file) {
      return {};
    }
    const auto& tracked_file = maybe_tracked_file.value().get();
    auto symbol_name = tracked_file.get_symbol_at_position(params.m_position);
    if (!symbol_name) {
      return {};
    }
    auto symbol_info = workspace.get_definition_info_from_all_types(symbol_name.value(),
                                                                    tracked_file.m_all_types_uri);
    if (!symbol_info) {
      return {};
    }
    LSPSpec::Location location;
    location.m_uri = tracked_file.m_all_types_uri;
    location.m_range.m_start = {(uint32_t)symbol_info.value().definition_info->line_idx_to_display,
                                (uint32_t)symbol_info.value().definition_info->pos_in_line};
    location.m_range.m_end = {(uint32_t)symbol_info.value().definition_info->line_idx_to_display,
                              (uint32_t)symbol_info.value().definition_info->pos_in_line};
    locations.push_back(location);
  } else if (file_type == Workspace::FileType::OpenGOAL) {
    auto maybe_tracked_file = workspace.get_tracked_og_file(params.m_textDocument.m_uri);
    if (!maybe_tracked_file) {
      return {};
    }
    const auto& tracked_file = maybe_tracked_file.value().get();
    const auto symbol = tracked_file.get_symbol_at_position(params.m_position);

    // Check if we are on a field in a (-> ...) form
    TSNode node = tracked_file.get_node_at_position(params.m_position);
    if (!ts_node_is_null(node) && std::string(ts_node_type(node)) == "sym_name") {
      TSNode parent = ts_node_parent(node);
      if (!ts_node_is_null(parent) && std::string(ts_node_type(parent)) == "list") {
        TSNode first_child = ts_node_child(parent, 1);
        if (!ts_node_is_null(first_child)) {
          std::string first_elt = ast_util::get_source_code(tracked_file.m_content, first_child);
          if (first_elt == "->") {
            // Find our index in the list
            int my_idx = -1;
            for (uint32_t i = 0; i < ts_node_child_count(parent); i++) {
              if (ts_node_eq(ts_node_child(parent, i), node)) {
                my_idx = i;
                break;
              }
            }

            if (my_idx >= 2) {
              std::string parent_type_name;
              if (my_idx == 2) {
                // Direct access: (-> obj field)
                TSNode prev_node = ts_node_child(parent, my_idx - 1);
                std::string prev_name = ast_util::get_source_code(tracked_file.m_content, prev_node);
                auto type_info = workspace.get_symbol_typeinfo(tracked_file, prev_name);
                if (type_info) {
                  parent_type_name = type_info->first.base_type();
                }
              } else {
                // Nested access: (-> obj f1 f2 ... prev node)
                TSNode obj_node = ts_node_child(parent, 2);
                std::string obj_name = ast_util::get_source_code(tracked_file.m_content, obj_node);
                auto type_info = workspace.get_symbol_typeinfo(tracked_file, obj_name);
                if (type_info) {
                  parent_type_name = type_info->first.base_type();
                  for (int i = 3; i < my_idx; i++) {
                    TSNode step_node = ts_node_child(parent, i);
                    std::string step_name =
                        ast_util::get_source_code(tracked_file.m_content, step_node);
                    auto step_field = workspace.get_field_info(tracked_file, parent_type_name, step_name);
                    if (step_field) {
                      parent_type_name = step_field->type;
                    } else {
                      parent_type_name = "";
                      break;
                    }
                  }
                }
              }

              if (!parent_type_name.empty()) {
                std::string field_name = ast_util::get_source_code(tracked_file.m_content, node);
                auto field_info =
                    workspace.get_field_info(tracked_file, parent_type_name, field_name);
                if (field_info && field_info->m_def_location) {
                  LSPSpec::Location location;
                  location.m_uri = lsp_util::uri_from_path(field_info->m_def_location->file_path);
                  location.m_range.m_start = {(uint32_t)field_info->m_def_location->line_idx,
                                              (uint32_t)field_info->m_def_location->char_idx};
                  location.m_range.m_end = {(uint32_t)field_info->m_def_location->line_idx,
                                            (uint32_t)field_info->m_def_location->char_idx};
                  locations.push_back(location);
                  return locations;
                }
              }
            }
          }
        }
      }
    }

    if (!symbol) {
      return {};
    }
    const auto& symbol_info = workspace.get_global_symbol_info(tracked_file, symbol.value());
    if (!symbol_info) {
      return {};
    }

    const auto& def_loc = workspace.get_symbol_def_location(tracked_file, symbol_info.value());
    if (!def_loc) {
      return {};
    }

    LSPSpec::Location location;
    #ifdef _WIN32
        location.m_uri = fmt::format("file:///{}", def_loc->file_path);
    #else
        location.m_uri = fmt::format("file://{}", def_loc->file_path);
    #endif
    location.m_range.m_start = {(uint32_t)def_loc->line_idx, (uint32_t)def_loc->char_idx};
    location.m_range.m_end = {(uint32_t)def_loc->line_idx, (uint32_t)def_loc->char_idx};
    locations.push_back(location);
  }

  return locations;
}
}  // namespace lsp_handlers
