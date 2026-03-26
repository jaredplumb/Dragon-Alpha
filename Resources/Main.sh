#!/bin/sh

cd "$(cd "$(dirname "$0")" && pwd)"

for path in Images Fonts Sounds; do
	if [ ! -d "$path" ]; then
		echo "Missing required resource directory: $path" >&2
		exit 1
	fi
done

echo "Dragon Alpha uses loose runtime resources; no package build is required."
