#include <stdio.h>

#include "hash.h"

int main(int argc, char *argv[])
{
    struct hashTable *t = hashInit(1024);

    hashPut(t, "name", "upyun");
    hashPut(t, "age", "10");
    hashPut(t, "color", "blue");

    printf("=== insert ===\n");
    printf("name: %s\n", hashGet(t, "name"));
    printf("age: %s\n", hashGet(t, "age"));
    printf("color: %s\n", hashGet(t, "color"));

    printf("=== update ===\n");
    hashPut(t, "age", "11");
    printf("age: %s\n", hashGet(t, "age"));

    hashDestroy(t);
    return 0;
}
