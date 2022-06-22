#ifndef owner_h
#define owner_h

int owner_kernel_init
(
    void
);

int owner_add_thread
(
    void *owner,
    void *thread
);

int owner_remove_thread
(
    void *owner,
    void *thread
);

#endif