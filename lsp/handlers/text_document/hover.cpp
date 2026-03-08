#include "hover.h"

#include "common/util/ast_util.h"

bool is_number(const std::string& s) {
  return !s.empty() && std::find_if(s.begin(), s.end(),
                                    [](unsigned char c) { return !std::isdigit(c); }) == s.end();
}

std::vector<std::string> og_method_names = {"new",      "delete", "print",    "inspect",  "length",
                                            "asize-of", "copy",   "relocate", "mem-usage"};

std::optional<LSPSpec::Hover> hover_handler_ir(Workspace& /*workspace*/,
                                               const LSPSpec::TextDocumentPositionParams& /*params*/,
                                               const WorkspaceIRFile& /*tracked_file*/) {
  return {};
}

std::string truncate_docstring(const std::string& docstring) {
  std::string truncated = "";
  const auto lines = str_util::split(docstring);
  for (const auto& line : lines) {
    const auto trimmed_line = str_util::ltrim(line);
    if (str_util::starts_with(trimmed_line, "@")) {
      break;
    }
    truncated += trimmed_line + "\n";
  }
  return truncated;
}

namespace lsp_handlers {
std::optional<json> hover(Workspace& workspace, int /*id*/, json raw_params) {
  auto params = raw_params.get<LSPSpec::TextDocumentPositionParams>();
  auto file_type = workspace.determine_filetype_from_uri(params.m_textDocument.m_uri);

  if (file_type == Workspace::FileType::OpenGOALIR) {
    auto tracked_file = workspace.get_tracked_ir_file(params.m_textDocument.m_uri);
    if (!tracked_file) {
      return {};
    }
    return hover_handler_ir(workspace, params, tracked_file.value());
  } else if (file_type == Workspace::FileType::OpenGOAL) {
    auto maybe_tracked_file = workspace.get_tracked_og_file(params.m_textDocument.m_uri);
    if (!maybe_tracked_file) {
      return {};
    }
    const auto& tracked_file = maybe_tracked_file.value().get();
    const auto symbol = tracked_file.get_symbol_at_position(params.m_position);

    // Check if we are on a field in a (-> ...) form
    TSNode node = tracked_file.get_node_at_position(params.m_position);
    
    // Normalize: if we are on a sym_name, move up to sym_lit if it exists
    if (!ts_node_is_null(node) && std::string(ts_node_type(node)) == "sym_name") {
      TSNode parent = ts_node_parent(node);
      if (!ts_node_is_null(parent) && std::string(ts_node_type(parent)) == "sym_lit") {
        node = parent;
      }
    }

    TSNode curr = node;
    int depth = 0;
    while (!ts_node_is_null(curr) && depth < 3) {
      std::string curr_type = ts_node_type(curr);
      lg::debug("hover - climbing depth {}: type={}", depth, curr_type);
      if (curr_type == "list" || curr_type == "form" || curr_type == "list_lit" || curr_type == "form_lit") {
        TSNode first_symbol = {{0, 0, 0, 0}};
        uint32_t search_limit = std::min(ts_node_child_count(curr), (uint32_t)3);
        for (uint32_t i = 0; i < search_limit; i++) {
          TSNode child = ts_node_child(curr, i);
          std::string c_type = ts_node_type(child);
          if (c_type == "sym_name" || c_type == "sym_lit") {
            first_symbol = child;
            break;
          }
        }

        if (!ts_node_is_null(first_symbol)) {
          std::string first_elt = ast_util::get_source_code(tracked_file.m_content, first_symbol);
          lg::debug("hover - first element in form: {}", first_elt);
          if (first_elt == "->") {
            lg::debug("hover - found -> operator");
            std::vector<TSNode> sym_nodes;
            int my_sym_idx = -1;
            for (uint32_t i = 0; i < ts_node_child_count(curr); i++) {
              TSNode child = ts_node_child(curr, i);
              std::string c_type = ts_node_type(child);
              if (c_type == "sym_name" || c_type == "sym_lit") {
                if (ts_node_eq(child, node)) {
                  my_sym_idx = (int)sym_nodes.size();
                }
                sym_nodes.push_back(child);
              }
            }
            lg::debug("hover - my symbol index in form: {}", my_sym_idx);

            if (my_sym_idx >= 2) { // 0 is ->, 1 is obj, 2+ are fields
              std::string parent_type_name;
              if (my_sym_idx == 2) {
                TSNode obj_node = sym_nodes[1];
                std::string obj_name = ast_util::get_source_code(tracked_file.m_content, obj_node);
                lg::debug("hover - resolving root object: {}", obj_name);
                auto type_info = workspace.get_symbol_typeinfo(tracked_file, obj_name);
                if (type_info) {
                  parent_type_name = type_info->first.base_type();
                  lg::debug("hover - root object type: {}", parent_type_name);
                }
              } else {
                TSNode obj_node = sym_nodes[1];
                std::string obj_name = ast_util::get_source_code(tracked_file.m_content, obj_node);
                lg::debug("hover - resolving nested chain starting at: {}", obj_name);
                auto type_info = workspace.get_symbol_typeinfo(tracked_file, obj_name);
                if (type_info) {
                  parent_type_name = type_info->first.base_type();
                  for (int i = 2; i < my_sym_idx; i++) {
                    std::string step_name = ast_util::get_source_code(tracked_file.m_content, sym_nodes[i]);
                    lg::debug("hover - resolving step {}: {} in type {}", i-1, step_name, parent_type_name);
                    auto step_field = workspace.get_field_info(tracked_file, parent_type_name, step_name);
                    if (step_field) {
                      parent_type_name = step_field->type;
                      lg::debug("hover - step {} resolved to type: {}", i-1, parent_type_name);
                    } else {
                      lg::debug("hover - step {} FAILED", i-1);
                      parent_type_name = "";
                      break;
                    }
                  }
                }
              }

              if (!parent_type_name.empty()) {
                std::string field_name = ast_util::get_source_code(tracked_file.m_content, node);
                if (std::string(ts_node_type(node)) == "sym_lit" && ts_node_child_count(node) > 0) {
                   for(uint32_t k=0; k < ts_node_child_count(node); k++) {
                      if(std::string(ts_node_type(ts_node_child(node, k))) == "sym_name") {
                         field_name = ast_util::get_source_code(tracked_file.m_content, ts_node_child(node, k));
                         break;
                      }
                   }
                }

                auto field_info = workspace.get_field_info(tracked_file, parent_type_name, field_name);
                if (field_info) {
                  LSPSpec::MarkupContent markup;
                  markup.m_kind = "markdown";
                  std::string body = fmt::format("### (field) {}.{} : `{}`\n", parent_type_name,
                                                 field_info->name, field_info->type);
                  if (!field_info->description.empty()) {
                    body += fmt::format("---\n{}", field_info->description);
                  }
                  markup.m_value = body;
                  LSPSpec::Hover hover_resp;
                  hover_resp.m_contents = markup;
                  return hover_resp;
                }
              }
            }
            break;
          }
        }
      }
      curr = ts_node_parent(curr);
      depth++;
    }

    if (!symbol) {
      lg::debug("hover - no symbol");
      return {};
    }
    // TODO - there is an issue with docstrings and overridden methods
    const auto& symbol_info = workspace.get_global_symbol_info(tracked_file, symbol.value());
    if (!symbol_info) {
      lg::debug("hover - no symbol info - {}", symbol.value());
      return {};
    }
    LSPSpec::MarkupContent markup;
    markup.m_kind = "markdown";

    const auto args = Docs::get_args_from_docstring(symbol_info.value()->m_args,
                                                    symbol_info.value()->m_docstring);
    std::string signature = "";
    bool takes_args = true;
    if (symbol_info.value()->m_kind == symbol_info::Kind::FUNCTION) {
      signature += "function ";
    } else if (symbol_info.value()->m_kind == symbol_info::Kind::METHOD) {
      signature += "method ";
    } else if (symbol_info.value()->m_kind == symbol_info::Kind::MACRO) {
      signature += "macro ";
    } else {
      takes_args = false;
    }
    // TODO - others useful, probably states?
    auto type_info = workspace.get_symbol_typeinfo(tracked_file, symbol.value());
    signature += symbol.value();
    if (takes_args) {
      signature += "(";
      for (int i = 0; i < (int)args.size(); i++) {
        const auto& arg = args.at(i);
        if (i == (int)args.size() - 1) {
          signature += fmt::format("{}: {}", arg.name, arg.type);
        } else {
          signature += fmt::format("{}: {}, ", arg.name, arg.type);
        }
      }
      signature += ")";
      if (symbol_info.value()->m_kind == symbol_info::Kind::FUNCTION && type_info) {
        signature += fmt::format(": {}", type_info->first.last_arg().base_type());
      } else if (symbol_info.value()->m_kind == symbol_info::Kind::METHOD) {
        signature +=
            fmt::format(": {}", symbol_info.value()->m_method_info.type.last_arg().base_type());
      }
    } else if (type_info) {
      signature += fmt::format(": {}", type_info->first.print());
    }

    std::string body = fmt::format("```opengoal\n{}\n```\n\n", signature);
    body += "___\n\n";
    if (!symbol_info.value()->m_docstring.empty()) {
      body += truncate_docstring(symbol_info.value()->m_docstring) + "\n\n";
    }

    // TODO - support @see/@returns/[[reference]]
    for (const auto& arg : args) {
      std::string param_line = "";
      if (arg.is_mutated) {
        param_line += fmt::format("*@param!* `{}: {}`", arg.name, arg.type);
      } else if (arg.is_optional) {
        param_line += fmt::format("*@param?* `{}: {}`", arg.name, arg.type);
      } else if (arg.is_unused) {
        param_line += fmt::format("*@param_* `{}: {}`", arg.name, arg.type);
      } else {
        param_line += fmt::format("*@param* `{}: {}`", arg.name, arg.type);
      }
      if (!arg.description.empty()) {
        param_line += fmt::format(" - {}\n\n", arg.description);
      } else {
        param_line += "\n\n";
      }
      body += param_line;
    }

    markup.m_value = body;
    LSPSpec::Hover hover_resp;
    hover_resp.m_contents = markup;
    return hover_resp;
  }

  return {};
}
}  // namespace lsp_handlers
