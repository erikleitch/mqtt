REBAR ?= ./rebar

all: deps
	${REBAR} compile

cpponly:
	./c_src/build_deps.sh get-deps
	./c_src/build_deps.sh mqttonly

get-deps:
	./c_src/build_deps.sh get-deps

deps:
	${REBAR} get-deps

rm-deps:
	\rm -rf deps
	./c_src/build_deps.sh rm-deps

clean:
	${REBAR} clean

include tools.mk
