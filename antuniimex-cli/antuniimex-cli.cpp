// antuniimex-cli.cpp : Definiert den Einstiegspunkt für die Konsolenanwendung.
//

#include "stdafx.h"

#include <cpprest/http_client.h>
#include <cpprest/filestream.h>
#include <cpprest/containerstream.h>
#include <cpprest/json.h>
#include <cpprest/producerconsumerstream.h>
#include <cpprest/rawptrstream.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <codecvt>
#include <windows.h>
#include <comdef.h>

using namespace utility;                    // Common utilities like string conversions
using namespace web;                        // Common features like URIs.
using namespace web::json;
using namespace web::http;                  // Common HTTP functionality
using namespace web::http::client;          // HTTP client features
using namespace concurrency::streams;       // Asynchronous streams
using namespace concurrency;

inline bool file_exists(const std::string& name) {
	struct stat buffer;
	return (stat(name.c_str(), &buffer) == 0);
}

pplx::task<void> getConfigurationList(const char *username, const char *password, const char *baseUrl) {
	http_client_config config;
	config.set_credentials(web::credentials(conversions::to_string_t(username), conversions::to_string_t(password)));

	http_client client(conversions::to_string_t(baseUrl), config);
	uri_builder builder(U("/AntUniimex"));

	return client.request(methods::GET, builder.to_string()).then([=](http_response response) -> pplx::task<json::value> {
		if (response.status_code() == status_codes::OK)
			return response.extract_json();
		else
			std::cout << "  # ERROR HTTP STATUS " << response.status_code() << std::endl;

		json::value t;
		return concurrency::task_from_result(t);

	}).then([=](pplx::task<json::value> previousTask) {
		json::value const & value = previousTask.get();
		if (value.is_object() && value.at(U("success")).as_bool()) {
			if (value.at(U("data")).is_array()) {
				for (unsigned int i = 0; i < value.at(U("data")).size(); i++) {
					auto item = value.at(U("data")).at(i);
					std::stringstream ss;
					std::cout << "  " << std::right << std::setw(3) << std::setfill(' ') << item.at(U("id")).as_integer() << "). " << utility::conversions::to_utf8string(item.at(U("name")).as_string()) << std::endl;
					if (item.at(U("statusIsRunning")).as_integer() == 1) {
						std::cout << "        - RUNNING: " << item.at(U("statusCurrentCount")).as_integer() << "/" << item.at(U("statusMaxCount")).as_integer() << std::endl;
						std::cout << "        - LastExec: " << conversions::to_utf8string(item.at(U("statusLastExecute")).as_string()) << std::endl;
						std::cout << std::endl;
					}
				}
			}
		}

		std::cout << "--------------------------------------------------------" << std::endl;
		std::cout << "done." << std::endl;
	});
}

pplx::task<void> runConfiguration(uri_builder builder, const char *username, const char *password, const char *baseUrl, string_t *guid = nullptr) {
	// TODO
	// ------------------------
	// 1. initital the config api/AntUniimex?action=init&configId=<id>
	// 2. call run url while returned json property done != true or property success != true
	http_client_config config;
	config.set_credentials(web::credentials(conversions::to_string_t(username), conversions::to_string_t(password)));

	http_client client(conversions::to_string_t(baseUrl), config);
	auto doneTask = [](){
		std::cout << "--------------------------------------------------------" << std::endl;
		std::cout << "done." << std::endl;
	};

	return client.request(methods::GET, builder.to_string()).then([=](http_response response) -> pplx::task<json::value> {
		if (response.status_code() == status_codes::OK)
			return response.extract_json();
		else
			std::cout << "  # ERROR HTTP STATUS " << response.status_code() << std::endl;

		json::value t;
		return concurrency::task_from_result(t);

	}).then([=](pplx::task<json::value> previousTask) {
		json::value const & value = previousTask.get();

		if (!value.is_null() && value.is_object() && value.at(U("success")).as_bool()) {
			string_t *_guid = (guid == nullptr) ? new string_t(value.at(U("guid")).as_string()) : guid;

			uri_builder builder(U("/AntUniimex"));
			builder.append_query(U("action"), U("run"));
			builder.append_query(U("guid"), _guid->c_str());

			if (!value.has_field(U("done")) && !value.has_field(U("finished"))) {
				std::cout << "  # starting configuration ..." << std::endl;
				return runConfiguration(builder, username, password, baseUrl, _guid);

			} else if (value.has_field(U("done")) && !value.at(U("done")).as_bool() && value.has_field(U("progressMessage")) && !value.has_field(U("finished"))) {
				if (value.at(U("progressPrecent")).as_double() < 1.0)
					std::cout << "  # " << conversions::to_utf8string(value.at(U("progressMessage")).as_string()) << std::endl;
				else {
					auto message = conversions::to_utf8string(value.at(U("progressMessage")).as_string());
					std::istringstream f(message.substr(5, message.length()-5).substr(0, message.length()-5-6));
					std::string line;
					std::cout << "  # ### LOG ############################################" << std::endl;
					while (std::getline(f, line)) {
						std::cout << "  # " << line << std::endl;
					}
					std::cout << "  # ### /LOG ###########################################" << std::endl;
				}

				return runConfiguration(builder, username, password, baseUrl, _guid);

			} else if (value.has_field(U("done")) && !value.at(U("done")).as_bool() && value.has_field(U("doPostInserts")) && value.at(U("doPostInserts")).as_bool() && !value.has_field(U("finished"))) {
				std::cout << " # processing post inserts " << value.at(U("processedBlocks")).as_integer() << "/" << value.at(U("maxBlocks")).as_integer() << std::endl;
				return runConfiguration(builder, username, password, baseUrl, _guid);

			} else if(value.has_field(U("finished"))) {
				if (!value.at(U("finished")).as_bool()) {
					std::cout << "  # starting export configuration ..." << std::endl;
					return runConfiguration(builder, username, password, baseUrl, _guid);
				} else {
					std::cout << "  # export done ..." << std::endl;

					if (value.at(U("downloadUrl")).is_string()) {
						std::cout << "  # starting download export.csv file ..." << std::endl;

						uri_builder builder(U("/AntUniimex"));
						builder.append_query(U("action"), U("download"));
						builder.append_query(U("guid"), _guid->c_str());

						http_client_config config;
						config.set_credentials(web::credentials(conversions::to_string_t(username), conversions::to_string_t(password)));
						http_client client(conversions::to_string_t(baseUrl), config);
						return client.request(methods::GET, builder.to_string()).then([=](http_response response) {
							size_t contentLength = response.headers().content_length();
							std::cout << "  # download size: " << contentLength << std::endl;

							auto fileStream = std::make_shared<ostream>();
							pplx::task<void> t = fstream::open_ostream(U("export.csv")).then([=](ostream outFile) {
								*fileStream = outFile;
								return response.body().read_to_end(fileStream->streambuf());
							}).then([=](size_t writeBytes) {
								return fileStream->close();
							});

							try {
								t.wait();
							} catch (const std::exception &e) {
								std::cout << "  # ERROR EXCEPTION: " << e.what() << std::endl;
							}
							
							return pplx::task<void>(doneTask);
						});
					}
				}
			}
		} else if(!value.is_null()) {
			if (value.has_field(U("done"))) {
				std::cout << "  # error on run configuration: " << conversions::to_utf8string(value.at(U("message")).as_string()) << std::endl;
			} else {
				std::cout << "  # error on initializing configuration: " << conversions::to_utf8string(value.at(U("message")).as_string()) << std::endl;
			}
		}

		return pplx::task<void>(doneTask);
	});
}

int main(int argc, char *argv[], char *envp[]) {
	const char *command = nullptr;
	int id = 0;
	const char *baseUrl = nullptr;
	const char *username = nullptr;
	const char *password = nullptr;

	std::cout << "Universal Importer Exporter - CLI v0.1 (beta)" << std::endl;
	std::cout << "--------------------------------------------------------" << std::endl;

	if (argc == 1) {
		std::cout << "  Commands:" << std::endl;
		std::cout << "    list           - list all configuration with config id" << std::endl;
		std::cout << "    run <configId> - starts a configuration" << std::endl;
		std::cout << std::endl;
		std::cout << "   Required arguments:" << std::endl;
		std::cout << "     /username - username of our shopware backend user" << std::endl;
		std::cout << "     /password - apikey from our shopware backend user" << std::endl;
		std::cout << "     /baseUrl  - base rest api url eg.: http://shopware.dev.lan/sw529/api/ (with trailing /)" << std::endl;
		std::cout << std::endl;
		std::cout << "--------------------------------------------------------" << std::endl;
		std::cout << std::endl;
		return 1;
	}

	WritePrivateProfileStringW(NULL, NULL, NULL, L"antuniimex-cli.ini");
	if (file_exists("antuniimex-cli.ini")) {
		WCHAR inBuf[255];
		WCHAR cfg_IniName[256];
		static char _username[255];
		static char _password[255];
		static char _baseUrl[255];

		GetCurrentDirectory(MAX_PATH, cfg_IniName);
#pragma warning(disable:4996)
		wcscat(cfg_IniName, L"\\antuniimex-cli.ini");

		{
			GetPrivateProfileString(TEXT("antuniimex"), TEXT("username"), TEXT(""), inBuf, 255, cfg_IniName);
			_bstr_t tmpBstr(inBuf);
			strncpy(_username, tmpBstr, 255 - 1);
			username = _username;

			GetPrivateProfileString(TEXT("antuniimex"), TEXT("password"), TEXT(""), inBuf, 255, cfg_IniName);
			tmpBstr = _bstr_t(inBuf);
			strncpy(_password, tmpBstr, 255 - 1);
			password = _password;

			GetPrivateProfileString(TEXT("antuniimex"), TEXT("baseUrl"), TEXT(""), inBuf, 255, cfg_IniName);
			tmpBstr = _bstr_t(inBuf);
			strncpy(_baseUrl, tmpBstr, 255 - 1);
			baseUrl = _baseUrl;
		}
	}

	const char *currentArgv = nullptr;
	if (argc > 1) {
		command = argv[1];
		int start = 2;
		if (strcmp(command, "run") == 0) {
			start = 3;
			id = atoi(argv[2]);
		}

		for (int i = start; i < argc; i++) {
			if (argv[i][0] == '/') {
				currentArgv = argv[i];
			}else if(currentArgv != nullptr) {
				if (strcmp(currentArgv, "/username") == 0) {
					username = argv[i];
				}else if (strcmp(currentArgv, "/password") == 0) {
					password = argv[i];
				}else if (strcmp(currentArgv, "/baseUrl") == 0) {
					baseUrl = argv[i];
				}
			}
		}
	}

	if (command == nullptr || username == nullptr || password == nullptr || baseUrl == nullptr) {
		std::cout << "ERROR: parameters username, passowrd, baseUrl are required!" << std::endl;
		std::cout << "usage: antuniimex-cli.exe <cmd> /username <username> /password <password> /baseUrl <baseUrl>" << std::endl;
		std::cout << "  eg.: antuniimex-cli.exe list /username demo /password apikey /baseUrl http://shopware.dev.lan/sw5214/api/" << std::endl;
		return 1;
	}

	pplx::task<void> task;
	if (strcmp(command, "list") == 0) {
		task = getConfigurationList(username, password, baseUrl);

	} else if (strcmp(command, "run") == 0) {
		std::cout << "  # initial configuration id <" << id << "> ..." << std::endl;

		uri_builder builder(U("/AntUniimex"));
		builder.append_query(U("action"), U("init"));
		builder.append_query(U("configId"), conversions::to_string_t(std::to_string(id)));

		task = runConfiguration(builder, username, password, baseUrl);
	} else {
		std::cout << "  # ERROR: command not found!" << std::endl;
		return 1;
	}

	// Wait for all the outstanding I/O to complete and handle any exceptions
	try {
		task.wait();
	} catch (const std::exception &e) {
		printf("ERROR EXCEPTION: %s\n", e.what());
		return 1;
	}

    return 0;
}

