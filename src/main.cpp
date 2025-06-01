#include <iostream>
#include <boost/asio.hpp>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <fstream>

using namespace std;

using boost::asio::ip::tcp;

#pragma pack(push, 1)
// Struct para definição do registro de leitura
struct LogRecord {
    char sensor_id[32];
    time_t timestamp;
    double value;
};
#pragma pack(pop)

// Função para converter uma string de data/hora no formato "AAAA-MM-DDTHH:MM:SS" para time_t
time_t string_to_time_t(const string& time_string) {
    tm tm = {};
    istringstream ss(time_string);
    ss >> get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return mktime(&tm);
};

// Função para converter um time_t para uma string de data/hora no formato "AAAA-MM-DDTHH:MM:SS"
string time_t_to_string(time_t time) {
    tm* tm = localtime(&time);
    ostringstream ss;
    ss << put_time(tm, "%Y-%m-%dT%H:%M:%S");
    return ss.str();
};

// Função para salvar o record no arquivo binário
void save_log_record_to_file(string filename, LogRecord record_to_save) {
    fstream file(filename, fstream::out | fstream::in | fstream::binary | fstream::app);
    if (file.is_open()) {
        int filesize = file.tellg();
        int num_records = filesize / sizeof(LogRecord);
        file.write((char*)&record_to_save, sizeof(LogRecord));
        file.close();
    } else {
        cout << "Error opening file, try again" << endl;
    };
};

// Função para ler os records do arquivo binário
string read_records(const string& filename, int num_records_to_read) {
    fstream file(filename, fstream::out | fstream::in | fstream::binary | fstream::app);
    if (file.is_open()) {
        int filesize = file.tellg();
        string converted_data = to_string(num_records_to_read);
        for (int i = 0; i < num_records_to_read; i++) {
            LogRecord record;
            file.read((char*)&record, sizeof(LogRecord));
            if (record.value >= -0.000001 && record.value <= 0.00001) {
                file.close();
                return "ERROR|INVALID_SENSOR_ID\r\n";
            };
            converted_data += ";" + time_t_to_string(record.timestamp) + "|" + to_string(record.value);
        };
        converted_data += "\r\n";
        file.close();
        return converted_data;
    } else {
        return "ERROR|INVALID_SENSOR_ID\r\n";
    };
};

// Função que divide uma string em substrings com base em um delimitador
vector<string> split_string_by_delimiter(const string& input_string, char delimiter_char) {
    vector<string> substrings;
    string substring;
    for (char c : input_string) {
        if (c != delimiter_char) {
            substring += c;
        } else if (!substring.empty()) {
            substrings.push_back(substring);
            substring.clear();
        };
    };
    if (!substring.empty()) {
        substrings.push_back(substring);
    };
    return substrings;
};

// Sessão
class session : public enable_shared_from_this<session> {
public:
    session(tcp::socket socket) : socket_(move(socket)) {}
    void start() {
        read_msg();
    };
    
private:
    void read_msg() {
        auto self(shared_from_this());
        boost::asio::async_read_until(socket_, buffer_, "\r\n",
            [this, self](boost::system::error_code error, size_t length) {
                if (!error) {
                    istream is(&buffer_);
                    string message(istreambuf_iterator<char>(is), {});
                    string reply_msg;
                    cout << message << endl;
                    vector<string> data = split_string_by_delimiter(message, '|');
                    if (data[0] == "LOG") {
                        LogRecord log;
                        strcpy(log.sensor_id, data[1].c_str());
                        log.timestamp = string_to_time_t(data[2]);
                        log.value = stod(data[3]);              
                        string filename = data[1] + ".dat";
                        save_log_record_to_file(filename, log);
                        write_msg(reply_msg);
                    } else if(data[0] == "GET") {
                        int num_reg = stoi(data[2]);
                        string filename = data[1] + ".dat";
                        reply_msg = read_records(filename, num_reg);
                        write_msg(reply_msg);
                    };
                };
            }
        );
    };
    void write_msg(const string& msg) {
        auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(msg),
            [this, self, msg](boost::system::error_code error, size_t length) {
                if (!error) {
                    read_msg();
                };
            }
        );
    };
    tcp::socket socket_;
    boost::asio::streambuf buffer_;
};

// Servidor
class server {
public:
    server(boost::asio::io_context& io_context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        accept();
    };

private:
    void accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code error, tcp::socket socket) {
                if (!error) {
                    make_shared<session>(move(socket))->start();
                };
                accept();
            }
        );
    };
    tcp::acceptor acceptor_;
};

// Main
int main(int argc, char* argv[]) {
    boost::asio::io_context io_context;
    server s(io_context, 9000);
    io_context.run();
    return 0;
};
