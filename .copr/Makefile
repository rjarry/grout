# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Robin Jarry

outdir ?= .
spec ?= rpm/grout.spec
spec_name = $(notdir $(spec))
out_spec = $(outdir)/$(spec_name)

git_version = $(shell git describe --long --abbrev=8 | sed 's/^v//;s/-/ /')
grout_version = $(firstword $(git_version))
grout_release = $(subst -g,%{dist}.,$(lastword $(git_version)))

dpdk_version = $(shell sed -nE 's/^source_filename = dpdk-(.+)\.tar\.xz$$/\1/p' subprojects/dpdk.wrap)
ecoli_version = $(shell sed -nE 's/^source_filename = libecoli-(.+)\.tar\.gz$$/\1/p' subprojects/ecoli.wrap)
frr_version = $(shell sed -nE 's/^source_filename = frr-(.+)\.tar\.gz$$/\1/p' subprojects/frr.wrap)

ifeq ($(spec_name),frr.spec)
archives = $(outdir)/frr-$(frr_version).tar.gz
else
archives = $(outdir)/dpdk-$(dpdk_version).tar.xz
archives += $(outdir)/libecoli-$(ecoli_version).tar.gz
archives += $(outdir)/grout-$(grout_version).tar.gz
endif

.PHONY: all
all: $(archives) $(out_spec)
	rpmbuild -bs -D '_srcrpmdir $(outdir)' -D '_sourcedir $(outdir)' \
		--rmspec --rmsource $(out_spec)

$(out_spec): $(spec)
	@mkdir -pv $(@D)
	sed -e 's/%{grout_version}/$(grout_version)/' \
		-e 's/%{grout_release}/$(grout_release)/' \
		-e 's/%{dpdk_version}/$(dpdk_version)/' \
		-e 's/%{ecoli_version}/$(ecoli_version)/' \
		-e 's/%{frr_version}/$(frr_version)/' \
		$< > $@

subprojects/packagecache/dpdk-$(dpdk_version).tar.xz:
	meson subprojects download dpdk

subprojects/packagecache/libecoli-$(ecoli_version).tar.gz:
	meson subprojects download ecoli

subprojects/packagecache/frr-$(frr_version).tar.gz:
	meson subprojects download frr

subprojects/packagecache/grout-$(grout_version).tar.gz:
	mkdir -p $(@D)
	git archive --prefix=grout-$(grout_version)/ -o $@ HEAD

$(outdir)/%: subprojects/packagecache/%
	install -m 644 -D $< $@

.PHONY: srpm
srpm:
	which git rpmbuild meson >/dev/null 2>&1 || dnf -y install meson git rpm-build
	$(MAKE) -f .copr/Makefile spec=$(spec) outdir=$(outdir)
