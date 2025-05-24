import subprocess
import os
import time
from itertools import product


def run_command(data_size, threads, tsize, input_file):
    cmd = [
        "./bin/MCC_030402_99106458",
        "--data_size", f"{data_size}K",
        "--threads", str(threads),
        "--tsize", f"{tsize}K",
        "--input", input_file
    ]

    try:
        print(f"\nRunning command: {' '.join(cmd)}")
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode == 0:
            print("Success!")
        else:
            print(f"Error: {result.stderr}")
    except Exception as e:
        print(f"Exception occurred: {str(e)}")


def main():
    data_sizes = [150, 300, 600]

    threads = [2 ** i for i in range(11)]

    tsize_multipliers = [2, 3, 4]

    base_dir = "./"

    total_combinations = len(data_sizes) * len(threads) * len(tsize_multipliers)
    current_combination = 0

    for data_size, thread_count, multiplier in product(data_sizes, threads, tsize_multipliers):
        current_combination += 1

        tsize = (data_size // 5) * multiplier

        input_file = os.path.join(base_dir, f"data/{data_size}K_set1.txt")

        print(f"\nProgress: {current_combination}/{total_combinations}")
        print(f"Running experiment with:")
        print(f"  Data Size: {data_size}K")
        print(f"  Threads: {thread_count}")
        print(f"  Table Size: {tsize}K")
        print(f"  Input File: {input_file}")

        run_command(data_size, thread_count, tsize, input_file)
        time.sleep(1)


if __name__ == "__main__":
    print("Starting experiments...")
    main()
    print("\nAll experiments completed!")
