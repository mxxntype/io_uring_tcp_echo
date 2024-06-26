cc := "gcc"
cflags := "-Wall -Werror -Wextra -D_GNU_SOURCE `pkg-config --libs liburing` `pkg-config --cflags liburing`"
target := "target/server"

default:
    @just --list

build:
    {{cc}} {{cflags}} *.c -o {{target}} -O2

run PORT: build
    {{target}} {{PORT}}
