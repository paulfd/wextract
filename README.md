# wextract

Cross-platform tool to extract wavetables and draw envelopes from sample files, exporting the wavetable and generating the appropriate SFZ text to use in [sfizz] or [Sforzando].

![Screenshot](picture.png)

## Usage:

- Open a file and select the left/right channel to use as a source.
- Select the range from which you would want to extract the wavetable.
- Set the root note and number of harmonics to gather at most.
- Extract the wavetable and compare with the source sample.
- You can build an envelope for the wavetable: use <kbd>Ctrl</kbd>+click to add envelope points on the source file, and double click to remove them. You can also set the sustain level, so that envelope points are computed relative to this level. The output SFZ file contains the flex envelope opcodes should you want to save them.
- You can save the wavetable when you're happy with it !

## Still to to:
- Frequency view
- Better signal processing
- Packaging

## Building

Clone the repository using the `--recursive` flag to gather all submodules.
If not, you will need to additionally run `git submodule update --init`.
The build process requires `cmake`.
The program depends on `libsfizz` [sfizz] on Linux; Windows dlls are bundled.

```sh
git clone https://github.com/paulfd/wextract --recursive
cd wextract
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j
```
