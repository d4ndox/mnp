#!/bin/bash

set -euo pipefail

mariadb --table \
    -e "SELECT * FROM payDB.payments;"
