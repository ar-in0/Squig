Strategy: For any problem, before re-inventing any existing functionality, you must be able to demostrate 
that standard solutions are inadequate. In other words:  
Problem: Extracting frames from a live RTMP stream  
S0: Larix Client -> Nginx-rtmp -> VLC 
- For playback on linux, use VLC network stream connected to the nginx server rtmp url
(same url that the larix client connects to)
---
[ref1](https://github.com/aileone/nginx-rtmp-module/issues/1), [ref2](https://obsproject.com/forum/resources/how-to-set-up-your-own-private-rtmp-server-using-nginx.50/#:~:text=Restart%20nginx%20with:,web%20site%20you%20set%20up.).  
This is alright for playback/rtmp demo, but its difficult to integrate OpenCV/vision algorithms into nginx server...  
```
sudo apt-get install build-essential libpcre3 libpcre3-dev libssl-dev
wget https://nginx.org/download/nginx-1.28.1.tar.gz
git clone https://github.com/ar-in0/nginx-rtmp-module

./configure --with-http_ssl_module --add-module=../nginx-rtmp-module
make
sudo make install

# apache for rmh.space running on port 80, so change the
# default nginx port to 81 in /etc/nginx/sites-enabled/
# Additionally, stop any instances of ubuntu nginx
sudo systemctl stop nginx # localhost:81 for nginx. COnfirm running with systemctl

# run nginx with rtmp module
sudo /usr/local/nginx/sbin/nginx 
```

Observations:
- ~4s delay between world action capture by larix client and display on machine. (is this client-side buffering or server-side or pure link-layer delay?)
- 

@21Jan
S1: Using ffmpeg command line (Just to demonstrate, obviously not suited to realtime processing.)  
With an nginx server, passed incoming rtmp to ffmpeg command line for decode and write to jpeg.
`ffmpeg -i rtmp://10.42.0.1/live/stream -r 1 out%03d.jpg`: Tested, output frames generated successfully and written to files.   
Video: assets/fmpeg-frame-extract.webm  
TODO: Pass rtmp to ffmpeg using squig server  


S2: Using OpenCV libraries (Might be viable)  
S3: Using libav* libraries directly in Squig (If OpenCV inadequate)  
