# Writes OUT, a C++ file with the bytes of FILE as torchlight::wizard::NAME and NAMESize.
file(READ "${FILE}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
file(WRITE "${OUT}" "#include <cstddef>\nnamespace torchlight::wizard {\nextern const unsigned char ${NAME}[];\nextern const size_t ${NAME}Size;\nconst unsigned char ${NAME}[] = {${hex}};\nconst size_t ${NAME}Size = sizeof ${NAME};\n}\n")
