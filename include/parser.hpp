#ifndef PARSER_HPP_
#define PARSER_HPP_

#include <string>
#include <vector>

namespace loki
{

struct Metric
{
	std::string_view name;
	std::string_view value;
	std::string_view help;
	std::string_view type;
	std::vector<std::pair<std::string_view, std::string_view>> labels;
};

class Parser
{
public:
	Parser(std::string_view source);
	std::vector<Metric> metrics();

	/// move cursor by `i` steps
	/// \brief hello there
	void step(int i);

	enum Token
	{ Attribute
	, AttributeType
	, AttributeKey
	, AttributeValue
	, MetricKey
	, MetricLabel
	, MetricLabelStart
	, MetricLabelKey
	, MetricLabelValue
	, MetricLabelEnd
	, MetricValue
	};

	/// lexer
	void tokenize();
	void scan_labels();
	std::string_view scan_untill(std::string_view delim);

	void new_token(Token token, std::string_view value);

	/// parser
	void parse();


private:
	std::string_view source_;
	int cursor_;

	std::vector<std::pair<Token, std::string_view>> tokens_;
	std::vector<Metric> metrics_;

};

} // namespace loki

#endif /* PARSER_HPP_ */
