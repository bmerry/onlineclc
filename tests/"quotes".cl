// Tests that __LINE__ and __FILE__ are set correctly
__constant int test_array1[__LINE__ == 2 ? 1 : -1] = {0};
__constant int test_array2[__FILE__[0] == '"' ? 1 : -1] = {0};
__constant int test_array3[__FILE__[7] == '"' ? 1 : -1] = {0};

__kernel void main(__global const half *in, __global half *out)
{
    *out = *in;
}
