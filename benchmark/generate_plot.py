import matplotlib.pyplot as plt
from pydantic.dataclasses import dataclass
from pathlib import Path
import argparse
import os

@dataclass
class ExtractFormat:
    name: str
    type: type
    sep: str
    idx: int
    units: str

# format = (starting token of line, type of data, index of value of interest in the string (separated by " "))
TPS = ExtractFormat("tps", float, " ", 2, "s")
DISPLAY_STATS = [TPS]

# indices of these values in the file names (split by _)
READ = 1
UPDATE = 2
ALPHA = 3

def extract_values(input_file: list[str], attrs: list[ExtractFormat]) -> dict:
    vals = {}
    att: ExtractFormat
    for att in attrs:
        for line in input_file:
            if line.startswith(att.name):
                val = att.type(line.split(att.sep)[att.idx])
                vals[att.name] = val
    return vals


def main():
    # parse inputs
    parser = argparse.ArgumentParser()
    parser.add_argument("input", help="location of input files")
    parser.add_argument("output", help="directory to output files to")
    args = parser.parse_args()
    
    # ensure output dir exists
    os.makedirs(args.output, exist_ok=True)
    
    read_update_dict: dict = {}

    for f in os.listdir(args.input):
        # get read and update weights
        read: str = str(f).split('_')[READ]
        update: str = str(f).split('_')[UPDATE]

        # read results and store them in the dict
        with open(Path(args.input) / f, 'r') as fd:
            if not read_update_dict.get(read):
                read_update_dict[read] = {}
            read_update_dict[read][update] = extract_values(fd.readlines(), DISPLAY_STATS)
    
    # print for debugging
    print(read_update_dict)

    # Create bar chart for each measurement over each skew
    for stat in DISPLAY_STATS:
        labels = []
        values = []
        # Iterate over every combination of read and update weight
        for r in read_update_dict.keys():
            for u in read_update_dict[r].keys():
                display_name = f"{r}/{u}"
                val = read_update_dict[r][u][stat.name]

                labels.append(display_name)
                values.append(val)

        chart_out = Path(args.output) / f'{stat.name}.png'

        # save bar chart
        plt.bar(labels, values)
        plt.xlabel('R/W Ratio')
        plt.ylabel(f'{stat.name} ({stat.units})')
        plt.title(f"{stat.name} over R/W Ratios")

        # Display the chart
        plt.savefig(chart_out)

if __name__ == "__main__":
    main()
