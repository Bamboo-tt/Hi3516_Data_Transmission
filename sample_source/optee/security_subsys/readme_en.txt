sample usage:

Notice:
Compilation of this sample depends on the compilation of the optee os and client.
For details about the optee dependency, please refer to its official document.

Step 1: compile sample code
    make -j;

Step 2: put ta to rootfs
    cp ta/*.ta /lib/optee_armtz

Step 3: run tee-supplicant in background
    ./tee-supplicant &

Step 4: run sample
    ./sample_cipher <index>

For details about how to use the optee, please refer to the document 《OP-TEE Development Guide》.