#ifndef BIGINT_H
#define BIGINT_H
#endif // BIGINT_H
#include <stdint.h>
#include <stdbool.h>


#define UNSIGNED 3
#define SIGNED 4
#define MSB 5
#define LSB 6
#define POSITIVE 7
#define NEGATIVE 8



//we will initially test a similar structure as GMP
//little endian in the sense that the first limb of
//body is the least significant and the last limb is the
//most significant.
//The header contains the sign bit, the body is the
//magnitude


typedef struct dap_bigint_64 {
    uint64_t *body;
} dap_bigint_64_t;

typedef struct dap_bigint_32 {
    uint32_t *body;
} dap_bigint_32_t;

typedef struct dap_bigint_16 {
    uint16_t *body;
} dap_bigint_16_t;

typedef struct dap_bigint_8 {
    uint8_t *body;
} dap_bigint_8_t;


//The below definition ASSUMES that the size is correctly defined.
//That also has an important bearing on the way functions are defined
//below.
typedef struct dap_bigint {
    long bigint_size; //in bits
    int limb_size;//in bits
    int endianess;
    int signedness;
    int sign;

    union {
        dap_bigint_64_t limb_64;
        dap_bigint_32_t limb_32;
        dap_bigint_16_t limb_16;
        dap_bigint_8_t limb_8;
    } data;
} dap_bigint_t;



//This function sets the limb of index limb_index.
//This function ASSUMES that the user is passing the CORRECT limb_index,
int dap_set_ith_limb_in_bigint(dap_bigint_t* a, unsigned int limb_index, void* limb_value_pointer){
    int limb_size=a->bigint_size;
    switch (limb_size) {
    case 8:
        a->data.limb_8.body[limb_index]=*((uint8_t*) limb_value_pointer);
        break;
    case 16:
        a->data.limb_16.body[limb_index]=*((uint16_t*) limb_value_pointer);
        break;
    case 32:
        a->data.limb_32.body[limb_index]=*((uint32_t*) limb_value_pointer);
        break;
    case 64:
        a->data.limb_64.body[limb_index]=*((uint64_t*) limb_value_pointer);
        break;
}
    return 0;

}

//Returns the length of the bigint, in the limb size set in the bigint.
long dap_get_bigint_limb_count(dap_bigint_t* a){
    long limb_count=a->bigint_size/a->limb_size;
    return limb_count;
}


//This function takes the limb of index "limb_index" from the bigint structure
//and returns it as a uint64_t value. This value is then used to populate the
//full adder structure for calculation.
uint64_t get_val_at_ith_limb_64(dap_bigint_t* a, int limb_index){

    uint64_t val_at_ith_limb=a->data.limb_64.body[limb_index];
    return val_at_ith_limb;
}

uint32_t get_val_at_ith_limb_32(dap_bigint_t* a, int limb_index){

    uint32_t val_at_ith_limb=a->data.limb_32.body[limb_index];
    return val_at_ith_limb;
}

uint16_t get_val_at_ith_limb_16(dap_bigint_t* a, int limb_index){

    uint16_t val_at_ith_limb=a->data.limb_16.body[limb_index];
    return val_at_ith_limb;
}

uint8_t get_val_at_ith_limb_8(dap_bigint_t* a, int limb_index){

    uint8_t val_at_ith_limb=a->data.limb_8.body[limb_index];
    return val_at_ith_limb;
}

int dap_check_2_bigint_limb_size_equal(dap_bigint_t* a, dap_bigint_t* b){

    return a->limb_size==b->limb_size;
}

int dap_check_3_bigint_limb_size_equal(dap_bigint_t* a, dap_bigint_t* b, dap_bigint_t* c){

    return (a->limb_size==b->limb_size)&&(b->limb_size==c->limb_size);
}

int dap_check_2_bigint_signedness(dap_bigint_t* a, dap_bigint_t* b){
    return (a->signedness==b->signedness);
}

int dap_check_3_bigint_signedness(dap_bigint_t* a, dap_bigint_t* b, dap_bigint_t* c){
    return (a->signedness==b->signedness)&&(b->signedness==c->signedness);
}


int dap_run_2_bigint_security_checks(dap_bigint_t* a, dap_bigint_t* b){

    if(dap_check_2_bigint_limb_size_equal(a,b)){
        return -1;
    }

    if(dap_check_2_bigint_signedness(a,b)){
        return -1;
    }

    return 0;

}

int dap_run_3_bigint_security_checks(dap_bigint_t* a, dap_bigint_t* b, dap_bigint_t* c){

    if(dap_check_3_bigint_limb_size_equal(a,b,c)){
        return -1;
    }

    if(dap_check_3_bigint_signedness(a,b,c)){
        return -1;
    }

    return 0;

}


