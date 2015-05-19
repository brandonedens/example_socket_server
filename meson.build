# Build file for server copyover example.
# Brandon Edens
# 2015-05-09

project('server_reload', 'c')
add_global_arguments('-std=gnu11', language : 'c')

dep_pthread = find_library('pthread')

executable('server_reload',
        sources: ['main.c', 'server.c', 'work_queue.c'],
        dependencies: dep_pthread)

