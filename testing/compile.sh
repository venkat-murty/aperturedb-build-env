
 g++ load.cpp -o load -std=c++2a -I/usr/local/include -L/usr/local/lib -laperturedb-client
 g++ query.cpp -o query  -std=c++2a -I/usr/local/include -L/usr/local/lib -laperturedb-client -lpthread
 g++ correctness.cpp -o correctness  -std=c++2a -I/usr/local/include -L/usr/local/lib -laperturedb-client -lpthread
