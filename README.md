Before working with the file install all this
run this command to get the required dependencies

sudo apt install ffmpeg libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libswscale-dev libomp-dev

For compilation :

g++ -o converter Converter.cpp -lavformat -lavcodec -lavutil -lswresample -lswscale -fopenmp

