tinyasm -f bin test\input0.asm -o test\input0.img
tinyasm -f bin test\input1.asm -o test\input1.img
tinyasm -f bin test\input2.asm -o test\input2.img
tinyasm -f bin test\fbird.asm -o test\fbird.img
tinyasm -f bin test\invaders.asm -o test\invaders.img
tinyasm -f bin test\pillman.asm -o test\pillman.img
tinyasm -f bin test\rogue.asm -o test\rogue.img -l test\rogue.lst
tinyasm -f bin test\rogue.asm -o test\rogue.com -l test\rogue.lst -dCOM_FILE=1
tinyasm -f bin test\os.asm -o test\os.img -l test\os.lst
tinyasm -f bin test\basic.asm -o test\basic.img -l test\basic.lst
