I need you to create a new unit test file, called prompt_eng_add.cc. This file will contain unit tests that will compare the output of addition in the Boost C++ library with the dap_bigint_2scompl_ripple_carry_adder_value function found at /home/anton/cellframe-sdk/dap-sdk/crypto/src/bigint/add.c. In the below, we will refer to the output of the dap_bigint_2scompl_ripple_carry_adder_value function as the “Libdap output”. Important: do not include more than ten headers for this task. 

Prompt_eng_add.cc will call both the Boost and Gtest libraries. The high level structure of the unit tests will include  the following attributes:

1) There will be a top level switch on the limb size (8,16,32,64) which will be a parameter called by the unit test suite. The test suite will call all tests for all limb sizes : 8,16,32,64. Specifically, this parameter is defined in /home/anton/cellframe-sdk/dap-sdk/crypto/src/bigint/bigint.h as the “limb_size” field in the “dap_bigint_t” type.

2) One level below the top level switch defined in 1), there will be a top level loop on the “bigint_size” field of the “dap_bigint_t” type, going one by one from 1 to 50000. This will define the size/length of the tested large integer for both the Boost and Libdap output.

3) In the loop defined in 2), the program will first define interesting arithmetic edge cases for the specific size of big integer in Hex. It will then:
	a) Declare constants of the “cpp_int” type in Boost and set them equal to the Hex constants. 
	b) Declare constants of the Libdap of the “dap_bigint_t” type. For a “bigint_size” value of 8, this would be 		
	done by calling a.data.limb_8.body if a is of the “dap_bigint_t” type. 
	c) Create a subloop that loops i from 0 to the “bigint_size” value defined in loop 2) above, and extracts the 	ith limb value from the “cpp_int” constant declared in 3.b). This value will then needs to be set as the 
	limb value of the “dap_bigint_t” type constant by using the dap_set_ith_limb_in_bigint function found at 	/home/anton/cellframe-sdk/dap-sdk/crypto/src/bigint/bigint.h.
	d) Conduct addition in Boost of the “cpp_int” type constants.
 	e) Conduct addition in Libdap using the  dap_bigint_2scompl_ripple_carry_adder_value function.
	f) Check the equality of the Boost and Libdap outputs. 