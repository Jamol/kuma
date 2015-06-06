
#include "HttpParser.h"
#include "util/kmtrace.h"
#include "util/util.h"
#include "Uri.h"

KUMA_NS_BEGIN

#define MAX_HTTP_HEADER_SIZE	10*1024*1024 // 10 MB

enum{
	HTTP_READ_LINE,
	HTTP_READ_HEAD,
	HTTP_READ_BODY,
	HTTP_READ_DONE,
	HTTP_READ_ERROR,
};

enum{
	CHUNK_READ_LENGTH,
	CHUNK_READ_BODY,
	CHUNK_READ_BODYRN,
};

//////////////////////////////////////////////////////////////////////////
HttpParser::HttpParser()
: is_request_(true)
, read_state_(HTTP_READ_LINE)
, header_complete_(false)
, has_content_length_(false)
, content_length_(0)
, is_chunked_(false)
, chunk_state_(CHUNK_READ_LENGTH)
, chunk_length_(0)
, chunk_read_length_(0)
, total_read_length_(0)
, status_code_(0)
, destroy_flag_ptr_(nullptr)
{

}

HttpParser::~HttpParser()
{
	param_map_.clear();
    header_map_.clear();
    if(destroy_flag_ptr_) {
        *destroy_flag_ptr_ = true;
    }
}

void HttpParser::reset()
{
	read_state_ = HTTP_READ_LINE;
	
	status_code_ = 0;
	content_length_ = 0;
	has_content_length_ = false;
	
	header_complete_ = false;
	is_chunked_ = false;
	chunk_state_ = CHUNK_READ_LENGTH;
	chunk_length_ = 0;
    chunk_read_length_ = 0;
	
	total_read_length_ = 0;
    str_buf_.clear();

	method_ = "";
	url_ = "";
	version_ = "";
    url_path_ = "";

	param_map_.clear();
    header_map_.clear();
}

bool HttpParser::complete()
{
	return HTTP_READ_DONE == read_state_;
}

bool HttpParser::error()
{
	return HTTP_READ_ERROR == read_state_;
}

int HttpParser::parse_data(uint8_t* data, uint32_t len)
{
	if(HTTP_READ_DONE == read_state_ || HTTP_READ_ERROR == read_state_) {
		KUMA_WARNTRACE("parse_data, invalid state="<<read_state_);
		return 0;
	}
	if(HTTP_READ_BODY == read_state_ && !is_chunked_ && !has_content_length_)
	{// return directly
		total_read_length_ += len;
		if(cb_data_) cb_data_(data, len);
		return len;
	}
    uint32_t read_len = 0;
    ParseState parse_state = parse_http(data, len, read_len);
    if(PARSE_STATE_DELETE == parse_state) {
        return read_len;
    }
	if(PARSE_STATE_ERROR == parse_state && cb_event_) {
		cb_event_(HTTP_ERROR);
	}
	return read_len;
}

int HttpParser::save_data(uint8_t* data, uint32_t len)
{
    if(0 == len) {
		return KUMA_ERROR_NOERR;
    }
    auto old_len = str_buf_.size();
    if(len + old_len > MAX_HTTP_HEADER_SIZE) {
		return -1;
    }
    str_buf_.append((char*)data, len);
	return KUMA_ERROR_NOERR;
}

HttpParser::ParseState HttpParser::parse_http(uint8_t* data, uint32_t len, uint32_t& used_len)
{
	char* cur_line = NULL;
	uint32_t& pos = used_len;
	pos = 0;

	if(HTTP_READ_LINE == read_state_)
	{// try to get status line
		while ((cur_line = get_line((char*)data, len, pos)) && cur_line[0] == '\0' && str_buf_.empty())
			;
		if(cur_line) {
			if(!parse_status_line(cur_line)) {
				read_state_ = HTTP_READ_ERROR;
				//if(m_sink) m_sink->on_http_error(0);
				return PARSE_STATE_ERROR;
			}
			read_state_ = HTTP_READ_HEAD;
		} else {
            // need more data
			if(save_data(data+pos, len-pos) != KUMA_ERROR_NOERR)
				return PARSE_STATE_ERROR;
			pos = len; // all data was consumed
			return PARSE_STATE_CONTINUE;
		}
	}
	if(HTTP_READ_HEAD == read_state_)
	{
		while ((cur_line = get_line((char*)data, len, pos)) != NULL)
		{
			if(cur_line[0] == '\0')
			{// blank line, header completed
				header_complete_ = true;
				if((has_content_length_ && 0 == content_length_ && !is_chunked_)
                          || is_equal("OPTIONS", method_))
				{
					read_state_ = HTTP_READ_DONE;
                    if(cb_event_) {
                        cb_event_(HTTP_HEADER_COMPLETE);
                        cb_event_(HTTP_COMPLETE);
                    }
					return PARSE_STATE_DONE;
				} else {
					if(content_length_ > 0) {
						is_chunked_ = false;
					}
					read_state_ = HTTP_READ_BODY;
					if(cb_event_) cb_event_(HTTP_HEADER_COMPLETE);
				}
				break;
			}
			parse_header_line(cur_line);
		}
		if(HTTP_READ_HEAD == read_state_)
		{// need more data
            if(save_data(data+pos, len-pos) != KUMA_ERROR_NOERR) {
				return PARSE_STATE_ERROR;
            }
			pos = len; // all data was consumed
		}
	}
	if(HTTP_READ_BODY == read_state_ && (len - pos) > 0)
	{// try to get body
		if(is_chunked_) {
			return parse_chunk(data, len, pos);
		} else {
			uint32_t cur_len = len - pos;
			if(has_content_length_ && (content_length_ - total_read_length_) <= cur_len)
			{// data enough
                uint8_t* notify_data = data + pos;
				uint32_t notify_len = content_length_ - total_read_length_;
                pos += notify_len;
				total_read_length_ = content_length_;
				read_state_ = HTTP_READ_DONE;
                bool destroyed = false;
                destroy_flag_ptr_ = &destroyed;
				if(cb_data_) cb_data_(notify_data, notify_len);
                if(destroyed) {
                    return PARSE_STATE_DELETE;
                }destroy_flag_ptr_ = nullptr;
                if(cb_event_) cb_event_(HTTP_COMPLETE);
				return PARSE_STATE_DONE;
			}
			else
			{// need more data
                uint8_t* notify_data = data + pos;
				total_read_length_ += cur_len;
                pos = len;
				if(cb_data_) cb_data_(notify_data, cur_len);
				return PARSE_STATE_CONTINUE;
			}
		}
	}
    return HTTP_READ_DONE == read_state_?PARSE_STATE_DONE:PARSE_STATE_CONTINUE;
}

bool HttpParser::parse_status_line(char* cur_line)
{
    const char* p_line = cur_line;
    if(!str_buf_.empty()) {
        str_buf_ += cur_line;
        p_line = str_buf_.c_str();
    }
    KUMA_INFOTRACE("parse_status_line, "<<p_line);
	if(is_request_) {// request
        const char*  p = strchr(p_line, ' ');
        if(p) {
            method_.assign(p_line, p);
            p_line = p + 1;
        } else {
            return false;
        }
        p = strchr(p_line, ' ');
        if(p) {
            url_.assign(p_line, p);
            p_line = p + 1;
        } else {
            return false;
        }
        version_ = p_line;
		decode_url();
		parse_url();
	} else {// response
        const char*  p = strchr(p_line, ' ');
        if(p) {
            version_.assign(p_line, p);
            p_line = p + 1;
        } else {
            return false;
        }
        std::string str;
        p = strchr(p_line, ' ');
        if(p) {
            str.assign(p_line, p);
        } else {
            str = p_line;
        }
		status_code_ = atoi(str.c_str());
	}
	clear_buffer();
	return true;
}

bool HttpParser::parse_header_line(char * cur_line)
{
	std::string str_name;
	std::string str_value;
    
    const char* p_line = cur_line;
    if(!str_buf_.empty()) {
        str_buf_ += cur_line;
        p_line = str_buf_.c_str();
    }
    const char* p = strchr(p_line, ':');
    if(NULL == p) {
        clear_buffer();
        return false;
    }
    str_name.assign(p_line, p);
    p_line = p + 1;
    str_value = p_line;
    clear_buffer();
	add_header_value(str_name, str_value);
	return true;
}

HttpParser::ParseState HttpParser::parse_chunk(uint8_t* data, uint32_t len, uint32_t& pos)
{
	while (pos < len)
	{
		if(CHUNK_READ_LENGTH == chunk_state_)
		{
			char* cur_line = get_line((char*)data, len, pos);
			if(NULL == cur_line)
			{// need more data, save remain data.
				if(save_data(data+pos, len-pos) != KUMA_ERROR_NOERR)
					return PARSE_STATE_ERROR;
				pos = len;
				return PARSE_STATE_CONTINUE;
			}
			if(!str_buf_.empty()) {
				str_buf_ += cur_line;
                chunk_length_ = strtol(str_buf_.c_str(), NULL, 16);
                clear_buffer();
            } else {
                chunk_length_ = strtol(cur_line, NULL, 16);
            }
			if(0 == chunk_length_)
			{// body end
				read_state_ = HTTP_READ_DONE;
                if(cb_event_) cb_event_(HTTP_COMPLETE);
				return PARSE_STATE_DONE;
			}
			chunk_read_length_ = 0;
			chunk_state_ = CHUNK_READ_BODY;
		}
		if(CHUNK_READ_BODY == chunk_state_)
		{
			uint32_t cur_len = len - pos;
			if(chunk_length_ - chunk_read_length_ <= cur_len)
			{// data enough
                uint8_t* notify_data = data + pos;
				uint32_t notify_len = chunk_length_ - chunk_read_length_;
				total_read_length_ += notify_len;
				chunk_read_length_ = chunk_length_ = 0; // reset
				chunk_state_ = CHUNK_READ_BODYRN;
                pos += notify_len;
                bool destroyed = false;
                destroy_flag_ptr_ = &destroyed;
				if(cb_data_) cb_data_(notify_data, notify_len);
                if(destroyed) {
                    return PARSE_STATE_DELETE;
                }
                destroy_flag_ptr_ = nullptr;
			}
			else
			{// need more data
                uint8_t* notify_data = data + pos;
				total_read_length_ += cur_len;
				chunk_read_length_ += cur_len;
                pos += cur_len;
				// no data left, needn't save
				if(cb_data_) cb_data_(notify_data, cur_len);
				return PARSE_STATE_CONTINUE;
			}
		}
		if(CHUNK_READ_BODYRN == chunk_state_)
		{
            if(str_buf_.length()+len-pos < 2)
            {// need more data
                if(save_data(data+pos, len-pos) != KUMA_ERROR_NOERR)
                    return PARSE_STATE_ERROR;
                pos = len;
                return PARSE_STATE_CONTINUE;
            }
			if(!str_buf_.empty()) {
				if(str_buf_[0] != '\r' || data[pos] != '\n')
				{
					KUMA_ERRTRACE("parse_chunk, can not find body rn");
					read_state_ = HTTP_READ_ERROR;
                    clear_buffer();
					//if(m_sink) m_sink->on_http_error(0);
					return PARSE_STATE_ERROR;
				}
				pos += 1;
                clear_buffer();
			} else {
				if(data[pos] != '\r' || data[pos+1] != '\n')
				{
					KUMA_ERRTRACE("parse_chunk, can not find body rn");
					read_state_ = HTTP_READ_ERROR;
					//if(m_sink) m_sink->on_http_error(0);
					return PARSE_STATE_ERROR;
				}
				pos += 2;
			}
            chunk_state_ = CHUNK_READ_LENGTH;
		}
	}
    return HTTP_READ_DONE == read_state_?PARSE_STATE_DONE:PARSE_STATE_CONTINUE;
}

bool HttpParser::decode_url()
{
    std::string new_url;
    int i = 0;
    auto len = url_.length();
    const char * p_str = url_.c_str();
    while (i < len)
    {
        switch (p_str[i])
        {
		case '+':
			new_url.append(1, ' ');
			i++;
			break;
			
		case '%':
			if (p_str[i + 1] == '%') {
                new_url.append(1, '%');
				i += 2;
			} else if(p_str[i + 1] == '\0') {
				return false;
			} else {
				char ch1 = p_str[i + 1];
				char ch2 = p_str[i + 2];
				ch1 = ch1 >= 'A' ? ((ch1 & 0xdf) - 'A' + 10) : ch1 - '0';
				ch2 = ch2 >= 'A' ? ((ch2 & 0xdf) - 'A' + 10) : ch2 - '0';
                new_url.append(1, (char)(ch1*16 + ch2));
				i += 3;
			}
			break;
			
		default:
			new_url = p_str[i++];
			break;
        }
    }
	
    std::swap(url_, new_url);
	return true;
}

bool HttpParser::parse_url()
{
    Uri uri;
    if(!uri.parse(url_)) {
        return false;
    }
    const std::string& query = uri.getQuery();
    std::string::size_type pos = 0;
    while (true) {
        auto pos1 = query.find(pos, '=');
        if(pos1 == std::string::npos){
            break;
        }
        std::string name(query.begin()+pos, query.begin()+pos1);
        pos = pos1 + 1;
        pos1 = query.find(pos, '&');
        if(pos1 == std::string::npos){
            std::string value(query.begin()+pos, query.end());
            add_param_value(name, value);
            break;
        }
        std::string value(query.begin()+pos, query.begin()+pos1);
        pos = pos1 + 1;
        add_param_value(name, value);
    }

	return true;
}

void HttpParser::add_param_value(const std::string& name, const std::string& value)
{
    if(!name.empty()) {
        param_map_[name] = value;
    }
}

void HttpParser::add_header_value(std::string& name, std::string& value)
{
    trim_left(name);
    trim_right(name);
    trim_left(value);
    trim_right(value);
    if(!name.empty()) {
        if(is_equal(name, "Content-Length")) {
            KUMA_INFOTRACE("add_header_value, Content-Length="<<value.c_str());
            content_length_ = atoi(value.c_str());
            has_content_length_ = true;
        } else if(is_equal(name, "Transfer-Encoding")) {
            KUMA_INFOTRACE("add_header_value, Transfer-Encoding="<<value.c_str());
            if(is_equal(value, "chunked")) {
                is_chunked_ = true;
            }
        } else if(is_equal(name, "Location")) {
            KUMA_INFOTRACE("add_header_value, Location="<<value.c_str());
            location_ = value;
        }
        header_map_[name] = value;
    }
}

const char* HttpParser::get_param_value(const char* p_name)
{
    auto it = param_map_.find((char*)p_name);
    if (it != param_map_.end()) {
        return (*it).second.c_str();
    }
    return NULL;
}

const char* HttpParser::get_header_value(const char* p_name)
{
	auto it = header_map_.find((char*)p_name);
	if (it != header_map_.end()) {
		return (*it).second.c_str();
	}
	return NULL;
}

void HttpParser::copy_param_map(STRING_MAP& param_map)
{
	param_map = param_map_;
}

void HttpParser::copy_header_map(STRING_MAP& header_map)
{
	header_map = header_map_;
}

char* HttpParser::get_line(char* data, uint32_t len, uint32_t& pos)
{
	bool b_line  = false;
	char *p_line = NULL;
	
	uint32_t n_pos = pos;
	
	while (n_pos < len && !b_line) {
		switch (data[n_pos])
		{
		case '\n' :	b_line = true;	break;
		case '\r' :	break;
		}
		n_pos ++;
	}
	
	if (b_line) {
		p_line = (char*)(data + pos);
		
		data[n_pos - 1] = 0;
		
		if (n_pos - pos >= 2 && data[n_pos - 2] == '\r')
			data[n_pos - 2] = 0;
		
		pos = n_pos;
	}
	
	return p_line;
}

KUMA_NS_END
