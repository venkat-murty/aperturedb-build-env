
CPPFLAGS="-I${HOME}/athena/client/cpp/include -I${HOME}/athena/utils/include -L${HOME}/athena/client/cpp/lib -L${HOME}/athena/utils/lib" 
LIBS="-laperturedb-client -lvdms-utils -lcomm -lpthread -lprotobuf -lglog -lgflags -lssl -lcrypto"

 g++ simple.cpp -o simple -std=c++2a ${CPPFLAGS} -I/usr/local/include -L/usr/local/lib ${LIBS} && \
 g++ load.cpp -o load -std=c++2a ${CPPFLAGS} -I/usr/local/include -L/usr/local/lib ${LIBS} && \
 g++ query.cpp -o query  -std=c++2a ${CPPFLAGS} -I/usr/local/include -L/usr/local/lib ${LIBS} && \
 g++ correctness.cpp -o correctness  -std=c++2a  ${CPPFLAGS} -I/usr/local/include -L/usr/local/lib ${LIBS}
