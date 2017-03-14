# toypython
A toy compiler based on LLVM API.

# Config and Compile

Required environment
```
sudo apt-get install llvm-3.8 clang-3.8
```
Build
```
make all
```

Generate from toypython source code
```
./toypython yoursourcefile
```

Execute your program from LLVM IR and print the result
```
lli-3.8 yoursourcefile.Output;echo $?
```
