#include "Connection.h"

#include <iostream>


#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <afina/logging/Service.h>
#include <sys/uio.h>

#include "protocol/Parser.h"


namespace Afina {
namespace Network {
namespace STnonblock {

// See Connection.h
void Connection::Start()
{
    alive = true;
    _logger->info("Starting connection {} on socket {}",_event.data.fd,_socket);
}

void Connection::setStorage(std::shared_ptr<Afina::Storage> &pStorage)
{
    this->pStorage = pStorage;
}

// See Connection.h
void Connection::OnError()
{
    alive = false;
    _logger->info("Error with connection {}",_event.data.fd);
}

// See Connection.h
void Connection::OnClose()
{
    alive = false;
    _logger->info("Closing connection {}",_event.data.fd);
}

// See Connection.h
void Connection::DoRead()
{
    int new_read_bytes = -1;
    try
    {
        while ((new_read_bytes = read(_socket, command_buf + read_bytes, sizeof(command_buf)) - read_bytes) > 0)
        {
            read_bytes += new_read_bytes;
            _logger->debug("Got {} bytes from socket", read_bytes);

            // Single block of data readed from the socket could trigger inside actions a multiple times,
            // for example:
            // - read#0: [<command1 start>]
            // - read#1: [<command1 end> <argument> <command2> <argument for command 2> <command3> ... ]
            while (read_bytes > 0)
            {
                _logger->debug("Process {} bytes", read_bytes);
                // There is no command yet
                if (!command_to_execute)
                {
                    std::size_t parsed = 0;
                    if (parser.Parse(command_buf, read_bytes, parsed))
                    {
                        // There is no command to be launched, continue to parse input stream
                        // Here we are, current chunk finished some command, process it
                        _logger->debug("Found new command: {} in {} bytes", parser.Name(), parsed);
                        command_to_execute = parser.Build(arg_remains);
                        if (arg_remains > 0)
                        {
                            arg_remains += 2;
                        }
                    }

                    // Parsed might fails to consume any bytes from input stream. In real life that could happens,
                    // for example, because we are working with UTF-16 chars and only 1 byte left in stream
                    if (parsed == 0)
                    {
                        break;
                    }
                    else
                    {
                        std::memmove(command_buf, command_buf + parsed, read_bytes - parsed);
                        read_bytes -= parsed;
                    }
                }

                // There is command, but we still wait for argument to arrive...
                if (command_to_execute && arg_remains > 0)
                {
                    _logger->debug("Fill argument: {} bytes of {}", read_bytes, arg_remains);
                    // There is some parsed command, and now we are reading argument
                    std::size_t to_read = std::min(arg_remains, std::size_t(read_bytes));
                    argument_for_command.append(command_buf, to_read);

                    std::memmove(command_buf, command_buf + to_read, read_bytes - to_read);
                    arg_remains -= to_read;
                    read_bytes -= to_read;
                }

                // Thre is command & argument - RUN!
                if (command_to_execute && arg_remains == 0)
                {
                    _logger->debug("Start command execution");

                    std::string result;//result of executing program

                    command_to_execute->Execute(*pStorage, argument_for_command, result);

                    //Forming answer
                    answer.push_back(result + "\r\n");

                    // Prepare for the next command
                    command_to_execute.reset();
                    argument_for_command.resize(0);
                    parser.Reset();

                    //End of forming answer.Changing connection event state(mask)
                    _event.events |= EPOLLOUT;
                }
            }
        }

        if (read_bytes == 0)
        {
            _logger->debug("Connection closed");
        }
        else if ((errno != EAGAIN) && (errno != EWOULDBLOCK))//could be unavailable
        {
            alive = false;
            throw std::runtime_error(std::string(strerror(errno)));
        }
    }
    catch (std::runtime_error &ex)
    {
        _logger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
    }
}

// See Connection.h
void Connection::DoWrite()
{
    //To achive atomic write for 'answer' array to fd(one system call)
    size_t answer_size,answers_fully_written;
    int new_written_bytes = -1;

    answer_size = answer.size();

    struct iovec out[answer_size];
    try{
        for(size_t i = 0; i < answer_size;++i)
        {
            out[i].iov_len = answer[i].size();
            out[i].iov_base = &answer[i][0];
        }

        out[0].iov_base = static_cast<char*>(out[0].iov_base) +  written_bytes;//offset of answer[j]

        if((new_written_bytes = writev(_socket,out,answer_size)) > 0)
        {
            written_bytes += new_written_bytes;

            //have to subtract sizes of fully written answer[j] and delete them from 'answer'
            answers_fully_written = 0;
            while((answers_fully_written < answer_size) && (written_bytes - answer[answers_fully_written].size() >= 0))
            {
                written_bytes -= answer[answers_fully_written].size();
                ++answers_fully_written;
            }

            if (answer.begin() + answers_fully_written > answer.end())
            {
                alive = false;
                throw std::runtime_error(std::string(strerror(errno)));
            }
            else
            {
                answer.erase(answer.begin(), answer.begin() + answers_fully_written);
            }

            if (answer.empty())//all done
            {
                _event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR;
            }
        }
        else
        {
            alive = false;
            throw std::runtime_error(std::string(strerror(errno)));
        }
    } catch (std::runtime_error &ex)
    {
        _logger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
    }
}

} // namespace STnonblock
} // namespace Network
} // namespace Afina
