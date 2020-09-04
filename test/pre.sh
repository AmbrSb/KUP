#! /bin/sh

for f in test-cases/tests.list
do
	(cat L && cat $f) > fx && mv fx $f
done
