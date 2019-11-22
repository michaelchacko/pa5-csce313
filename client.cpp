#include "common.h"
#include "BoundedBuffer.h"
#include "Histogram.h"
#include "HistogramCollection.h"
#include "NRC.h"
using namespace std;


//Global HistogramCollection pointer
HistogramCollection* hc;

struct args_t {
    uint requestsPerPatient;    /* -n option [1, 15K] */
    uint numberOfPatients;      /* -p option [1,15]   */
    uint numberOfWorkerThreads; /* -w option [50, 5K]*/
    uint boundedBufferSize;     /* -b option [1, 300] */
    string filename;            /* -f option */
    string hostname;            /* -h option */
    string port;                /* -r option */
    bool fileReq;
} args;

struct patientArgs_t {
    BoundedBuffer* bb; // a pointer to data in a buffer
    HistogramCollection* hist;
    args_t clargs;
};

struct workerArgs_t {
    BoundedBuffer* bb;
    HistogramCollection* hist;
    args_t clargs;
};

void signal_handler(int dummy) {
    signal(SIGALRM, signal_handler);
    system("clear");
    if(hc != nullptr) {
        hc->print();
    }
    alarm(2);
}

void processArgs(int argc, char *argv[]){
    //TODO check ranges for arguments
    /* Set default arguments */
    args.requestsPerPatient = 1;
    args.numberOfPatients = 1;
    args.numberOfWorkerThreads = 1;
    args.boundedBufferSize = 1;
    args.fileReq = false;
    args.filename = "";

    const char *optString = "n:p:w:b:f:h:r:?";
    int opt = getopt(argc, argv, optString);
    while(opt != -1) {
        switch(opt) {
            case 'n':
                args.requestsPerPatient = atoi(optarg);
                if(!(1 <= args.requestsPerPatient && args.requestsPerPatient <= 15000)) {
                    cerr << "n parameter bounds [1,15000]\n";
                    exit(-1);
                }
                break;

            case 'p':
                args.numberOfPatients = atoi(optarg);
                if(!(1 <= args.numberOfPatients && args.numberOfPatients <= 15)) {
                    cerr << "p parameter bounds [1,15]\n";
                    exit(-1);
                }
                break;

            case 'w':
                args.numberOfWorkerThreads = atoi(optarg);
                if(!(1 <= args.numberOfWorkerThreads && args.numberOfWorkerThreads <= 5000)) {
                    cerr << "w parameter bounds [1,5000]\n";
                    exit(-1);
                }
                break;

            case 'b':
                args.boundedBufferSize = atoi(optarg);
                if(!(1 <= args.boundedBufferSize && args.boundedBufferSize <= 300)) {
                    cerr << "b parameter bounds [1,300]\n";
                    exit(-1);
                }
                break;

            case 'f':
                args.filename = optarg;
                args.fileReq = true;
                break;

            case 'h':
                args.hostname = optarg;
                break;

            case 'r':
                args.port = optarg;
                break;

            case '?':
                cerr << "ERROR: ? unknown argument" << endl;
                break;

            default:
                cerr << "ERROR: default unknown argument" << endl;
                break;
        }

        opt = getopt(argc, argv, optString);
    }
}

void* filereq_function(patientArgs_t* patientArgs, int remaining_length)
{
    BoundedBuffer* bb = patientArgs->bb;
    ofstream fout;

    __int64_t offset = 0; //offset for each filemsg cwrite call

    string filename = patientArgs->clargs.filename;

    // get the file
    while(remaining_length > 0) {

        //set the buffer size
        int bufSize = MAX_MESSAGE;
        if(remaining_length < MAX_MESSAGE) {
            bufSize = remaining_length;
        }

        /* create filemsg and push its contents to the boundedbuffer*/
        filemsg fmsg(offset, bufSize);
        vector<char> list((char*)&fmsg, ((char *)&fmsg + sizeof(fmsg))); //convert the fmsg object into a vector of chars
        copy(filename.begin(), filename.end(), std::back_inserter(list)); //add the name the filename to the back of the vector of chars
        list.push_back('\0'); //push null terminator
        bb->push(list); //push the vector of chars to the bounded buffer

        offset += bufSize;
        remaining_length -= bufSize;
    }
}

void* patient_function(patientArgs_t* patientArgs, uint patientNumber)
{
    BoundedBuffer* bb = patientArgs->bb;
    HistogramCollection* hc = patientArgs->hist;

    //add chars to the bounded buffer
    for(int i=0; i < patientArgs->clargs.requestsPerPatient;i++){
        datamsg dmsg(patientNumber, i*0.004, 1);
        vector<char> list((char*)&dmsg, ((char *)&dmsg + sizeof(dmsg)));
        bb->push(list);
    }
}

void* worker_function(workerArgs_t* workerArgs, NRC* newChannel)
{
    BoundedBuffer* bb = workerArgs->bb;
    HistogramCollection* hc = workerArgs->hist;
    string received_file = "received/" + workerArgs->clargs.filename;

    //run until we get a quit message
    while(true){
        vector<char> data = bb->pop();
        /* Get the char* msg and send it to the server */
        char* msg = reinterpret_cast<char*>(data.data());
        newChannel->cwrite((char *) msg, data.size());

        /* Figure out what kind of message it is */
        MESSAGE_TYPE m = *(MESSAGE_TYPE *) msg;
        if(m == QUIT_MSG){
            /* Exit worker thread */
            break;
        }
        else if(m == DATA_MSG){
            datamsg *dmsg = (datamsg *) msg;
            /* Get the data point */
            char* buf = newChannel->cread();
            double dataPoint = *(double *) buf;

            hc->update(dmsg->person, dataPoint);

            delete[] buf;
        }
        else if(m == FILE_MSG){
            filemsg *fmsg = (filemsg *) msg;
            char* buf = newChannel->cread();

            /*
             * The file we want to write to was already allocated in main.
             * Now we want to modify portions of it with the correct data.
             */
            int fd = open(received_file.c_str(), O_RDWR, 0777);

            if(fd == -1) {
                printf("worker_thread::open()! %s\n", strerror(errno));
                break;
            }
            lseek(fd, fmsg->offset, SEEK_SET);
            write(fd, buf, fmsg->length);
            close(fd);

            delete[] buf;
        }
        else if(m == NEWCHANNEL_MSG){
            cerr << "worker_function error: new channel message found\n";
            break;
        }
        else if(m == UNKNOWN_MSG){
            cerr << "worker_function error: unknown message found\n";
            break;
        }
        else {
            cerr << "worker_function error: unknown message found\n";
            break;
        }
    }

    delete newChannel;
}

int main(int argc, char *argv[])
{
	int m = MAX_MESSAGE; 	// default capacity of the file buffer
    srand(time_t(NULL));
    processArgs(argc, argv);

    string command = "ulimit -n 5000";
    system(command.c_str());

	hc = new HistogramCollection();

    if(!args.fileReq) {
        signal(SIGALRM, signal_handler);
        alarm(2);
    }

    BoundedBuffer* request_buffer = new BoundedBuffer(args.boundedBufferSize);

    patientArgs_t patientArgs = {request_buffer, hc, args};
    workerArgs_t workerArgs = {request_buffer, hc, args};
    vector<thread> patientThreads, workerThreads;

    struct timeval start, end;
    gettimeofday (&start, 0);

    /* Start all patient threads here */
    if(args.fileReq) {

        //Setup the special filmsg(0,0) request to ask the server for the length of a given file
        filemsg *fmsg = new filemsg(0, 0);
        const int payloadSize = sizeof(filemsg) + args.filename.length() + 1;
        char payload[payloadSize]; //create char array with size payloadSize
        *(filemsg *) payload = *fmsg; //set the first segment to the filemsg object
        strcpy(payload + sizeof(filemsg), args.filename.c_str()); // set the final segment to the filename string
        NRC tmpChan(args.hostname, args.port);
        tmpChan.cwrite((char*) &payload, payloadSize);
        delete fmsg;

        //get the file length
        char* buf = tmpChan.cread();
        int remaining_length = *(int *) buf;
        cout << "remaining length"  << remaining_length << endl;
        delete[] buf;


        //send quit message to our tmpchan
        MESSAGE_TYPE q = QUIT_MSG;
        tmpChan.cwrite ((char *) &q, sizeof (MESSAGE_TYPE));

        //allocate space for the file to write to
        string received_file = "received/" + args.filename;
        int fd = open(received_file.c_str(), O_RDWR | O_CREAT, 0777);
        if(fd == -1) {
            printf("Oh dear, something went wrong with create()! %s\n", strerror(errno));
            return -1;
        }
        if(lseek(fd, remaining_length-1, SEEK_SET) == 1){
            printf("Oh dear, something went wrong with lseek()! %s\n", strerror(errno));
        }
        close(fd);

        /* file request. just one special patient thread needed */
        patientThreads.push_back(thread(filereq_function, &patientArgs, remaining_length));
    }
    else {
        /* Make a patient thread for each data file */
        for(int i=0; i<args.numberOfPatients;i++){
            hc->add(new Histogram(10,-5,5)); //make a histogram for each patient
            patientThreads.push_back(thread(patient_function, &patientArgs, i+1));
        }
    }

    MESSAGE_TYPE message = NEWCHANNEL_MSG;
    for(int i=0; i<args.numberOfWorkerThreads;i++){
        // Create a new channel for the worker thread
        NRC* newChannel = new NRC(args.hostname, args.port);
        workerThreads.push_back(thread(worker_function, &workerArgs, newChannel));
    }

	/* Join all patient threads here */
    for(thread & t : patientThreads){
        if(t.joinable())
            t.join();
    }

    /* Now that all the patient threads have pushed their data to the buffer
       we can push a quit message for every worker */
    for(int i=0;i<workerThreads.size();i++){
        //send quit message
        MESSAGE_TYPE q = QUIT_MSG;
        vector<char> list((char*)&q, ((char *)&q + sizeof(q)));
        request_buffer->push(list);
    }

    /* Join all the worker threads */
    for(thread & t : workerThreads){
        if(t.joinable())
            t.join();
    }

    gettimeofday (&end, 0);
    if(!args.fileReq){
        system("clear");
    }
	hc->print ();
    int secs = (end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)/(int) 1e6;
    int usecs = (int)(end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)%((int) 1e6);
    cout << "Took " << secs << " seconds and " << usecs << " micro seconds" << endl;

    // cleanup
    delete request_buffer;

}
