#include "manager.h"
#include "service.h"
#include "chef.h"
#include "client.h"

int main() {
    start_manager();
    start_service();
    start_chef();
    start_client_generator();
    return 0;
}