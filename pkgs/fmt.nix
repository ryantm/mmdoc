{
  writeScriptBin,
  clang-tools,
  alejandra,
}:
writeScriptBin "fmt" ''
  echo "Formatting C code..."
  find src -iname *.h -o -iname *.c | xargs ${clang-tools}/bin/clang-format -i
  echo "Formatting Nix code..."
  alejandra -q .
''
