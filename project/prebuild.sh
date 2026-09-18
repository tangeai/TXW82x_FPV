#!/bin/bash
rm -f Obj/common_common.o
# 匹配规则解释：
# ^[[:space:]]*        : 行首可以有空格或制表符
# #define[[:space:]]+  : 必须以 #define 开头，后面至少跟一个空格
# CUSTOMER_ID[[:space:]]+ : 匹配 CUSTOMER_ID 及其后的空格
# 3([[:space:]]|$)     : 匹配数字 3，且 3 后面必须是空格或者行尾（防止匹配到 33）

if grep -E -q "^[[:space:]]*#define[[:space:]]+CUSTOMER_ID[[:space:]]+3([[:space:]]|$)" project_config.h; then
      cp config_xt215.cfg config.cfg
elif grep -E -q "^[[:space:]]*#define[[:space:]]+CUSTOMER_ID[[:space:]]+4([[:space:]]|$)" project_config.h; then
      cp config828.cfg config.cfg
fi
