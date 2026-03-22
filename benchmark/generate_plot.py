import matplotlib.pyplot as plt
from pydantic.dataclasses import dataclass
from pathlib import Path
import argparse
import os
import numpy as np

@dataclass
class ExtractFormat:
    name: str
    type: type
    sep: str
    idx: int
    units: str
    display_name: str

# format = (starting token of line, type of data, index of value of interest in the string (separated by " "))
TPS = ExtractFormat("tps", float, " ", 2, "", "Transactions / Second")
RATIO = ExtractFormat("ratio", float, " ", 1, "", "hits / total")
DISPLAY_STATS = [TPS , RATIO]


# indices of these values in the file names (split by _)
READ = 1
UPDATE = 2

# for bar charts
patterns = ['/', '+'] 

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
    parser.add_argument("title", help="chart title")
    args = parser.parse_args()
    
    # ensure output dir exists
    os.makedirs(args.output, exist_ok=True)
    
    input_dirs = args.input.split(" ")
    
    read_update_dict: dict = {}

    for dir in input_dirs:
        read_update_dict[dir] = {}
        for f in os.listdir(dir):
            # get read and update weights
            read: str = str(f).split('_')[READ]
            update: str = str(f).split('_')[UPDATE]

            # read results and store them in the dict
            with open(Path(dir) / f, 'r') as fd:
                if not read_update_dict.get(dir).get(read):
                    read_update_dict.get(dir)[read] = {}
                read_update_dict.get(dir)[read][update] = extract_values(fd.readlines(), DISPLAY_STATS)
    
    # print for debugging
    print(read_update_dict)

    # Create bar chart for each measurement over each skew
    for stat in DISPLAY_STATS:
        labels = []
        values = {}
        for dir in input_dirs:
            values[dir] = []
            labels_loc = []
            # Iterate over every combination of read and update weight
            for r in sorted(read_update_dict.get(dir).keys()):
                for u in sorted(read_update_dict.get(dir)[r].keys()):
                    display_name = f"{r}/{u}"
                    values[dir].append(read_update_dict.get(dir)[r][u][stat.name])
                    labels_loc.append(display_name)
            labels.append(labels_loc)
        labels = labels[0]
        
        print(values)

        chart_out = Path(args.output) / f'{stat.name}.png'
        
        x = np.arange(len(labels))
        width = 0.25
        multiplier = 0
        fig, ax = plt.subplots(layout='constrained')
        measure_max = 0
        pattern = 0
        for attr, measurement in values.items():
            offset = width*multiplier
            rects = ax.bar(x+offset, measurement, width, label=attr)
            for rect in rects:
                rect.set_hatch(patterns[pattern])
                pattern += 1
                pattern %= len(patterns)
            ax.bar_label(rects, padding=3)
            measure_max = max(measure_max, max(measurement))
            multiplier += 1
        
        print(values)

        # save bar chart
        ax.set_xlabel('R/W Ratio')
        ax.set_ylabel(f'{stat.display_name} ({stat.units})')
        ax.set_xticks(x + width/2, labels)
        ax.legend(loc='upper left', ncols=3)
        ax.set_ylim(0, measure_max*1.25)
        plt.title(f"{args.title} {stat.name}")
        # Display the chart
        plt.savefig(chart_out)
        plt.close()

if __name__ == "__main__":
    main()
