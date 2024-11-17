#include "Id.h"

int Id()
{
    static int id = 0;
    return id++;
}