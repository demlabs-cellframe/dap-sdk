#!/bin/bash

files=(*)
for (( i=0; i<${#files[@]}; i++ )); do
    file1=${files[i]}
    for (( j=i+1; j<${#files[@]}; j++ )); do
    file2=${files[j]}
        if [ "$file1" != "$file2" ] && [ "$file1" != "$(basename $0)" ] && [ "$file2" != "$(basename $0)" ]; then
        echo "Comparing $file1 and $file2"
        diff "$file1" "$file2"
        fi
    done
done