import argparse

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("table", help="table to use")
    parser.add_argument("output", help="png file to output to")
    args = parser.parse_args()

    args.table
    args.output

if __name__ == "__main__":
    main()
