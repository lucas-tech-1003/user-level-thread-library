# User-Level Thread Library

## Description

This project is a user-level thread library implemented in C. It provides basic threading functionalities, including thread creation, synchronization, and scheduling.

## Library Functionalities

### Thread Creation

The library allows you to create new threads using the `thread_create` function. Each thread runs a specific function passed as an argument during its creation.

### Preemptive Scheduling

The library implements preemptive scheduling using simulated timer interrupts. This ensures that threads are switched automatically without requiring explicit yielding.

### Thread Sleep and Wakeup

The `thread_sleep` function allows a thread to block itself for a specified duration or until a specific condition is met. The `thread_wakeup` function can be used to unblock a sleeping thread.

### Thread Waiting

The `thread_wait` function allows one thread to wait for another thread to finish its execution. This is useful for synchronizing the completion of tasks.

### Mutual Exclusion and Synchronization

The library provides mutex locks and condition variables to handle mutual exclusion and synchronization between threads. Mutex locks ensure that only one thread can access a critical section at a time, while condition variables allow threads to wait for certain conditions to be met.

## Installation

1. Clone the repository:
    ```sh
    git clone https://github.com/yourusername/user-level-thread-library.git
    ```
2. Navigate to the project directory:
    ```sh
    cd user-level-thread-library
    ```
3. Build the project:
    ```sh
    make
    ```

## Usage

1. Include the thread library in your C project:
    ```c
    #include "thread.h"
    ```
2. Create and manage threads using the provided API.

## Contributing

Contributions are welcome! Please follow these steps to contribute:

1. Fork the repository.
2. Create a new branch:
    ```sh
    git checkout -b feature-branch
    ```
3. Make your changes and commit them:
    ```sh
    git commit -m "Description of your changes"
    ```
4. Push to the branch:
    ```sh
    git push origin feature-branch
    ```
5. Create a pull request.

## License

This project is licensed under the MIT License.