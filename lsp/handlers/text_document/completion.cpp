#include "completion.h"

#include "common/util/ast_util.h"

namespace lsp_handlers {

std::unordered_map<symbol_info::Kind, LSPSpec::CompletionItemKind> completion_item_kind_map = {
    {symbol_info::Kind::CONSTANT, LSPSpec::CompletionItemKind::Constant},
    {symbol_info::Kind::FUNCTION, LSPSpec::CompletionItemKind::Function},
    {symbol_info::Kind::FWD_DECLARED_SYM, LSPSpec::CompletionItemKind::Reference},
    {symbol_info::Kind::GLOBAL_VAR, LSPSpec::CompletionItemKind::Variable},
    {symbol_info::Kind::INVALID, LSPSpec::CompletionItemKind::Text},
    {symbol_info::Kind::LANGUAGE_BUILTIN, LSPSpec::CompletionItemKind::Function},
    {symbol_info::Kind::MACRO, LSPSpec::CompletionItemKind::Operator},
    {symbol_info::Kind::METHOD, LSPSpec::CompletionItemKind::Method},
    {symbol_info::Kind::TYPE, LSPSpec::CompletionItemKind::Class},
};

std::optional<json> get_completions(Workspace& workspace, int /*id*/, json params) {
  auto converted_params = params.get<LSPSpec::CompletionParams>();
  const auto file_type = workspace.determine_filetype_from_uri(converted_params.textDocument.m_uri);

  if (file_type != Workspace::FileType::OpenGOAL) {
    return nullptr;
  }
  auto maybe_tracked_file = workspace.get_tracked_og_file(converted_params.textDocument.m_uri);
  if (!maybe_tracked_file) {
    return nullptr;
  }
  std::vector<LSPSpec::CompletionItem> items;
  const auto& tracked_file = maybe_tracked_file.value().get();
  // The cursor position in the context of completions is always 1 character ahead of the text, we
  // move it back 1 spot so we can actually detect what the user has typed so far
  LSPSpec::Position new_position = converted_params.position;
  if (new_position.m_character > 0) {
    new_position.m_character--;
  }
  const auto symbol = tracked_file.get_symbol_at_position(new_position);
  if (!symbol) {
    lg::debug("get_completions - no symbol to work from");

    // Check if we are inside a (-> ...) form
    TSNode node = tracked_file.get_node_at_position(new_position);
    while (!ts_node_is_null(node)) {
      if (std::string(ts_node_type(node)) == "list") {
        TSNode first_child = ts_node_child(node, 1);  // index 0 is '(', index 1 is first element
        if (!ts_node_is_null(first_child) && std::string(ts_node_type(first_child)) == "sym_name") {
          std::string first_elt = ast_util::get_source_code(tracked_file.m_content, first_child);
          if (first_elt == "->") {
            // We are in a (-> ...) form!
            // Find which argument we are typing
            int arg_idx = -1;
            TSNode cursor_node = tracked_file.get_node_at_position(new_position);
            // If we are at a space, the node might be the list itself or a sibling
            for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
              TSNode child = ts_node_child(node, i);
              if (ts_node_start_byte(child) >= ts_node_start_byte(cursor_node)) {
                arg_idx = i;
                break;
              }
            }
            if (arg_idx == -1) {
              arg_idx = ts_node_child_count(node);
            }

            if (arg_idx >= 3) {
              // We are typing a field. We need to resolve the type of the thing at arg_idx - 1
              TSNode obj_node = ts_node_child(node, 2);
              std::string obj_name = ast_util::get_source_code(tracked_file.m_content, obj_node);
              auto type_info = workspace.get_symbol_typeinfo(tracked_file, obj_name);
              if (type_info) {
                std::string current_type = type_info->first.base_type();
                for (int i = 3; i < arg_idx; i++) {
                  TSNode step_node = ts_node_child(node, i);
                  std::string step_name = ast_util::get_source_code(tracked_file.m_content, step_node);
                  auto step_field = workspace.get_field_info(tracked_file, current_type, step_name);
                  if (step_field) {
                    current_type = step_field->type;
                  } else {
                    current_type = "";
                    break;
                  }
                }

                if (!current_type.empty()) {
                  const auto fields = workspace.get_field_suggestions(tracked_file, current_type);
                  for (const auto& field : fields) {
                    LSPSpec::CompletionItem item;
                    item.label = field.name;
                    item.kind = LSPSpec::CompletionItemKind::Field;
                    item.detail = field.type;
                    items.push_back(item);
                  }
                }
              }
            }
            break;
          }
        }
      }
      node = ts_node_parent(node);
    }
  } else {
    const auto matching_symbols =
        workspace.get_symbols_starting_with(tracked_file.m_game_version, symbol.value());
    lg::debug("get_completions - found {} symbols", matching_symbols.size());

    for (const auto& symbol : matching_symbols) {
      LSPSpec::CompletionItem item;
      item.label = symbol->m_name;
      item.kind = completion_item_kind_map.at(symbol->m_kind);
      // TODO - flesh out this more fully when auto-complete with non-globals works as well
      items.push_back(item);
    }
  }
  LSPSpec::CompletionList list_result;
  list_result.isIncomplete = false;  // we want further typing to re-evaluate the list
  list_result.items = items;
  return list_result;
}

}  // namespace lsp_handlers
