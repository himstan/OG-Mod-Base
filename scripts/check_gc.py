import sys
import re
import argparse

def check_gc_parentheses(code):
    lines = code.splitlines()
    stack = []  # Stores tuples of (line_num, col_num)
    
    in_string = False
    in_comment = False
    escape_next = False
    
    # Keywords that indicate the start of a new top-level block
    top_level_keywords = {"defun", "define", "defstate", "defmethod", "define-extern", "defbehavior"}
    current_top_level = None

    for line_idx, line in enumerate(lines):
        col_idx = 0
        
        while col_idx < len(line):
            char = line[col_idx]
            
            # 1. Skip characters inside comments
            if in_comment:
                break 
            
            # 2. Handle string literals
            if in_string:
                if escape_next:
                    escape_next = False
                elif char == '\\':
                    escape_next = True
                elif char == '"':
                    in_string = False
                col_idx += 1
                continue
                
            # 3. Detect comments
            if char == ';':
                in_comment = True
                break
                
            # 4. Detect strings
            if char == '"':
                in_string = True
                col_idx += 1
                continue
                
            # 5. Process Opening Parenthesis
            if char == '(':
                # Peek ahead to see the keyword
                match = re.match(r'\s*([a-zA-Z0-9_-]+)', line[col_idx+1:])
                word = match.group(1) if match else None
                
                # SMART DETECTION FIX: 
                # It is only a top-level block if it's a known keyword AND starts exactly at column 0.
                # This prevents false positives on nested definitions like local defmethods.
                if word in top_level_keywords and col_idx == 0:
                    
                    if len(stack) > 0 and current_top_level:
                        return False, (
                            f"Error: Missing closing parenthesis ')' for the top-level form '{current_top_level[2]}' "
                            f"starting at line {current_top_level[0]}.\n"
                            f"Detected because a new top-level '{word}' was started at line {line_idx + 1} "
                            f"while {len(stack)} parenthesis were still left open."
                        )
                    
                    # Track the start of this new top-level block
                    current_top_level = (line_idx + 1, col_idx + 1, word)
                    
                stack.append((line_idx + 1, col_idx + 1))
                
            # 6. Process Closing Parenthesis
            elif char == ')':
                if not stack:
                    return False, f"Error: Extra closing parenthesis ')' found at line {line_idx + 1}, column {col_idx + 1}."
                
                stack.pop()
                
                # If stack is empty, we successfully closed the top level block
                if len(stack) == 0:
                    current_top_level = None 

            col_idx += 1
        
        # Reset comment flag at the end of the line
        in_comment = False 

    # 7. Final EOF Check
    if stack:
        if current_top_level:
            return False, (
                f"Error: Reached end of file with {len(stack)} unclosed parenthesis '('.\n"
                f"Likely missing in the top-level form '{current_top_level[2]}' starting at line {current_top_level[0]}.\n"
                f"Deepest unclosed '(' is at line {stack[-1][0]}, column {stack[-1][1]}."
            )
        else:
            return False, f"Error: Reached end of file with unclosed parenthesis. First unclosed '(' at line {stack[0][0]}, column {stack[0][1]}."

    return True, "Success: Parentheses are perfectly balanced!"

def main():
    parser = argparse.ArgumentParser(description="A smart parenthesis checker for .gc (GOAL) files.")
    parser.add_argument("filepath", help="Path to the .gc file to check")
    args = parser.parse_args()

    try:
        with open(args.filepath, 'r', encoding='utf-8') as f:
            code = f.read()
    except FileNotFoundError:
        print(f"File not found: {args.filepath}")
        sys.exit(1)

    is_valid, message = check_gc_parentheses(code)
    
    print(message)
    if not is_valid:
        sys.exit(1)

if __name__ == "__main__":
    main()