#include "agent.hpp"

#include <snappy.h>

#include "detail/utils.hpp"
#include "logproto.pb.h"
#include <ctime>
#include <cstdio>

namespace loki
{

Agent::Agent(const std::map<std::string, std::string> &labels,
			 int flush_interval,
			 int max_buffer,
			 Level log_level,
			 Level print_level,
			 Protocol protocol,
			 std::array<TermColor, 4> colors)
	: labels_{labels}
	, flush_interval_{flush_interval}
	, max_buffer_{max_buffer}
	, log_level_{log_level}
	, print_level_{print_level}
	, protocol_{protocol}
	, colors_{colors}
	, logs_{}
{
	switch (protocol_) {
	case Protocol::Protobuf:
		compiled_labels_ = "{";
		for (const auto &label : labels_) {
			compiled_labels_ += label.first;
			compiled_labels_ += "=\"";
			compiled_labels_ += label.second;
			compiled_labels_ += "\",";
		}
		compiled_labels_.pop_back();
		compiled_labels_ += "}";
		break;
	case Protocol::Json:
		compiled_labels_ = "{";
		for (const auto &label : labels_) {
			compiled_labels_ += "\"";
			compiled_labels_ += label.first;
			compiled_labels_ += "\":\"";
			compiled_labels_ += label.second;
			compiled_labels_ += "\",";
		}
		compiled_labels_.pop_back();
		compiled_labels_ += "}";
		break;
	}
	curl_ = curl_easy_init();
}

Agent::~Agent()
{
	curl_easy_cleanup(curl_);
	curl_ = nullptr;
}

bool Agent::Done()
{
	std::lock_guard<std::mutex> lock{mutex_};
	return !logs_.empty();
}

void Agent::Log(const std::string &line, Level level)
{
	mutex_.lock();

	timespec now;
	std::timespec_get(&now, TIME_UTC);

	if (logs_.size() <= max_buffer_) {
		mutex_.unlock();
		Flush();
		mutex_.lock();
	}

	if (log_level_ <= level) {
		logs_.emplace(std::make_pair(now, line));
	}

	// moving this into a separate function?
	// adding the possibilty to define a custom callback?
	if (print_level_ <= level) {
		auto repr = [](Level level) -> std::string {
			switch (level) {
			case Level::Debug:   return "[DEBUG]";
			case Level::Info:    return "[ INFO]";
			case Level::Warn:    return "[ WARN]";
			case Level::Error:   return "[ERROR]";
			case Level::Disable: return "";
			}
		};

		auto color = [this](Level level) -> int {
			return static_cast<int>(colors_[static_cast<int>(level)]);
		};

		char buf[100];
		std::strftime(buf, sizeof buf, "%F %T", std::gmtime(&now.tv_sec));
		fmt::print("\033[{}m{}.{:09} {} {}\033[0m\n", color(level), std::string(buf), now.tv_nsec, repr(level), line);
	}

	mutex_.unlock();
}

void Agent::Flush()
{
	switch (protocol_) {
	case Protocol::Protobuf:
		FlushProto();
		break;
	case Protocol::Json:
		FlushJson();
		break;
	}
}

void Agent::FlushJson()
{
	std::lock_guard<std::mutex> lock{mutex_};

	int count = 0;
	std::string line = "[";
	while (!logs_.empty()) {
		const auto &[t, s] = logs_.front();
		line += R"([")";
		line += detail::to_string(t);
		line += R"(",")";
		for (const auto &c : s) {
			switch (c) {
			case '\\':
				line += "\\";
				break;
			case '"':
				line += "\"";
				break;
			default:
				line += s;
				break;
			}
		}
		line += R"("],)";
		logs_.pop();
		count++;
	}
	line.pop_back();
	line += "]";

	if (count) {
		std::string payload;
		payload += R"({"streams":[{"stream":)";
		payload += compiled_labels_;
		payload += R"(,"values":)";
		payload += line;
		payload += R"(}]})";
		detail::http::post(curl_, "http://127.0.0.1:3100/loki/api/v1/push", payload);
	}
}

void Agent::FlushProto()
{
	using namespace logproto;

	std::lock_guard<std::mutex> lock{mutex_};

	std::string payload;
	std::string compressed;
	PushRequest push_request;

	auto *stream = push_request.add_streams();
	stream->set_labels(compiled_labels_);

	while (!logs_.empty()) {
		const auto &[t, s] = logs_.front();

		auto *timestamp = new google::protobuf::Timestamp{};
		timestamp->set_seconds(t.tv_sec);
		timestamp->set_nanos(t.tv_nsec);

		auto *entries = stream->add_entries();
		entries->set_line(s);
		entries->set_allocated_timestamp(timestamp);

		logs_.pop();
	}

	push_request.SerializeToString(&payload);
	snappy::Compress(payload.data(), payload.size(), &compressed);
	detail::http::post(curl_, "http://127.0.0.1:3100/loki/api/v1/push", compressed);
}

} // namespace loki
