# PNG Metadata Extractor for Stable Diffusion

The console will prompt you to paste into the console a path to a folder of PNG files. 

Then the program should scan the images' metadata, focusing only on tEXt chunks with buffered reading.
This seems to be the best way to extract metadata from Stable Diffusion gens.

Then the program should generate inside the targeted folder for each image a .txt file which contains the tEXt chunks of metadata. each .txt file should have the same title as the .png file the metadata comes from.

Enjoy!
