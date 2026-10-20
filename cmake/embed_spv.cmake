# cmake -DIN=x.spv -DOUT=x.spv.inc -DVAR=name -P embed_spv.cmake : writes the SPIR-V as a C array.
file(READ ${IN} hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," arr "${hex}")
file(WRITE ${OUT} "static const unsigned char ${VAR}[] = {${arr}};\n")
