# Writes INPUT's bytes to OUTPUT as a C array named SYMBOL, with its length in
# SYMBOL_SIZE:  cmake -DINPUT=... -DOUTPUT=... -DSYMBOL=... -P embed-binary.cmake
file(READ ${INPUT} hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE ${OUTPUT} "#include <stddef.h>
extern \"C\" {
extern const unsigned char ${SYMBOL}[] = {${bytes}};
extern const size_t ${SYMBOL}_SIZE = sizeof(${SYMBOL});
}
")
