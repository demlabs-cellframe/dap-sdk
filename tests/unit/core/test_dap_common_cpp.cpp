#include <cstring>

#include "dap_common.h"

typedef struct test_dup_cpp {
    int value;
    char name[16];
} test_dup_cpp_t;

int main()
{
    int l_value = 1;
    int *l_value_dup = DAP_DUP(&l_value);
    if (!l_value_dup || *l_value_dup != l_value)
        return 1;
    DAP_DELETE(l_value_dup);

    int *l_sized_dup = DAP_DUP_SIZE(&l_value, sizeof(l_value));
    if (!l_sized_dup || *l_sized_dup != l_value)
        return 2;
    DAP_DELETE(l_sized_dup);

    test_dup_cpp_t l_src = { 42, "dap_dup" };
    test_dup_cpp_t *l_struct_dup = DAP_DUP(&l_src);
    if (!l_struct_dup || l_struct_dup->value != l_src.value || std::strcmp(l_struct_dup->name, l_src.name))
        return 3;
    DAP_DELETE(l_struct_dup);

    return 0;
}
